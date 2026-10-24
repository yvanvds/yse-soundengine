// Tests for the ~stepcounter patcher object (patcher/analysis/dStepCounter),
// issue #1080. The counting is DSP::stepCounter's and is tested in
// Tests/dsp/test_step_counter.cpp; these show that the signal, numbers,
// lists, messages and arguments reach it, alone and in a rendered patch after
// a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dStepCounter.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/stepCounter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::stepCounter;
using YSE::PATCHER::dStepCounter;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct sinks {
    BufferSink out;
    BufferSink count;
    BufferSink step;
  };

  void Wire(dStepCounter& obj, sinks& s) {
    BufferSink* all[] = {&s.out, &s.count, &s.step};
    for (unsigned int i = 0; i < 3; ++i) {
      obj.ConnectOutlet(all[i]->GetInlet(0), i);
      all[i]->ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  unsigned int Impulses(buffer& b) {
    unsigned int count = 0;
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (b.getPtr()[i] == 1.f) ++count;
    return count;
  }

  // The object's three outputs against the reference's for one block.
  void Same(sinks& s, stepCounter& reference, buffer& in) {
    REQUIRE(s.out.received != nullptr);
    REQUIRE(s.count.received != nullptr);
    REQUIRE(s.step.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*s.out.received, reference(in), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.count.received, reference.counts(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.step.received, reference.steps(), 0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~stepcounter: registry and shape (#1080)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_STEPCOUNTER);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~stepcounter");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_STEPCOUNTER));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~stepcounter: numbers, lists and messages against the DSP class (#1080, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    sinks s;
    dStepCounter obj;
    Wire(obj, s);
    stepCounter reference;
    // A clock of 32 samples per cycle (1500 Hz), for the object and the
    // reference: N / 32 jumps per block.
    phasor objSource;
    phasor refSource;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      buffer& in = objSource(1500.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      Same(s, reference, refSource(1500.f, N));
    };

    // Default: an empty sequence, every jump fires.
    step();
    CHECK(Impulses(*s.out.received) == N / 32 - 1); // no jump at sample 0
    step();
    CHECK(Impulses(*s.out.received) == N / 32);

    // A list: the sequence.
    obj.GetInlet(0)->SetList("2 1 3", YSE::T_GUI);
    const float seq[] = {2.f, 1.f, 3.f};
    reference.sequence(seq, 3);
    step();
    step();

    // A number replaces it with one count; so does an int.
    obj.GetInlet(0)->SetFloat(4.f, YSE::T_GUI);
    const float four[] = {4.f};
    reference.sequence(four, 1);
    step();
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    reference.sequence(seq, 1);
    step();

    // seq, and seq with nothing empties it.
    obj.SetMessage("seq 3 2", 0.f);
    const float threeTwo[] = {3.f, 2.f};
    reference.sequence(threeTwo, 2);
    step();
    obj.SetMessage("seq", 0.f);
    reference.sequence(nullptr, 0);
    step();
    CHECK(Impulses(*s.out.received) == N / 32);

    // A command that arrives as a list (text through a receive) is a command.
    obj.GetInlet(0)->SetList("seq 5", YSE::T_GUI);
    const float five[] = {5.f};
    reference.sequence(five, 1);
    step();

    // reset, then startmode 0.
    obj.SetMessage("reset", 0.f);
    reference.reset();
    step();
    obj.SetMessage("startmode 0", 0.f);
    obj.SetMessage("reset", 0.f);
    reference.startMode(false).reset();
    step();

    // direction 1: a rising phasor's resets fall, so nothing fires.
    obj.SetMessage("direction 1", 0.f);
    reference.direction(stepCounter::UP);
    step();
    CHECK(Impulses(*s.out.received) == 0);
    obj.SetMessage("direction 2", 0.f);
    reference.direction(stepCounter::DOWN);
    step();

    // thresh above the reset: nothing fires.
    obj.SetMessage("thresh 0.99", 0.f);
    reference.threshold(0.99f);
    step();
    CHECK(Impulses(*s.out.received) == 0);
    obj.SetMessage("thresh 0.5", 0.f);
    reference.threshold(0.5f);
    step();

    // syncupdate 2: a new sequence waits for the end of this one.
    obj.SetMessage("syncupdate 2", 0.f);
    obj.SetMessage("seq 1", 0.f);
    const float one[] = {1.f};
    reference.syncUpdate(stepCounter::AFTER_SEQUENCE).sequence(one, 1);
    step();
    step();
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("seq 2 2", 0.f);
    const float twoTwo[] = {2.f, 2.f};
    reference.syncUpdate(stepCounter::AFTER_STEP).sequence(twoTwo, 2);
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(stepCounter::IMMEDIATE);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("direction", 0.f);
    obj.SetMessage("direction 3", 0.f);
    obj.SetMessage("syncupdate -1", 0.f);
    obj.SetMessage("startmode x", 0.f);
    obj.SetMessage("thresh nan", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Unconnected: the inlet reads 0, which never jumps.
    sinks idleOut;
    dStepCounter idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.out.received != nullptr);
    CHECK(Impulses(*idleOut.out.received) == 0);
  }

  TEST_CASE("~stepcounter: arguments are the sequence, with the attributes as keywords (#1080)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float hz, stepCounter& reference) {
      CAPTURE(args);
      sinks s;
      dStepCounter obj;
      Wire(obj, s);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objSource;
      phasor refSource;
      for (int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        buffer& in = objSource(hz, N);
        obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        Same(s, reference, refSource(hz, N));
      }
    };

    const float seq[] = {2.f, 1.f, 3.f};
    stepCounter plain;
    plain.sequence(seq, 3);
    check("2 1 3", 1500.f, plain);

    // The keywords anywhere among the counts; a falling ramp counted up.
    stepCounter up;
    up.sequence(seq, 3).direction(stepCounter::UP).startMode(false);
    check("2 direction 1 1 startmode 0 3", -1500.f, up);
    stepCounter rest;
    rest.sequence(seq, 3).syncUpdate(stepCounter::AFTER_STEP).threshold(0.995f);
    check("syncupdate 1 thresh 0.995 2 1 3", 1500.f, rest);

    // A keyword without a number, or out of range, is skipped.
    stepCounter skipped;
    skipped.sequence(seq, 3);
    check("2 direction 7 1 3 syncupdate", 1500.f, skipped);

    stepCounter none;
    check("", 1500.f, none);
  }

  TEST_CASE("~stepcounter: in a rendered patch, a ~phasor clock with messages from a receive, "
            "after a JSON round trip (#1080)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "1500");
      YSE::pHandle* counter = src.CreateObject(YSE::OBJ::D_STEPCOUNTER, "direction 2 2 1 3");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1080");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(counter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, counter, 0);
      src.Connect(cmd, 0, counter, 0);
      src.Connect(counter, 0, dac, 0);
      src.Connect(counter, 2, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    stepCounter reference;
    const float seq[] = {2.f, 1.f, 3.f};
    reference.sequence(seq, 3).direction(stepCounter::DOWN);
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData("4", "cmd1080", YSE::T_GUI));
        const float four[] = {4.f};
        reference.sequence(four, 1);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("reset", "cmd1080", YSE::T_GUI));
        reference.reset();
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(1500.f, N)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.steps(), 0.f));
      if (block == 1) {
        // Jumps every 32 samples; 2 1 3 fires on 3 of every 6.
        CHECK(Impulses(dst.output[0]) > 0);
        CHECK(Impulses(dst.output[0]) < N / 32);
      }
      if (block == 4) {
        // After the reset, the first reset of the phasor fires.
        CHECK(dst.output[0].getPtr()[0] == 1.f);
      }
    }
  }

  TEST_CASE("~stepcounter: numbers, lists, messages and blocks on the audio thread allocate "
            "nothing (#1080)") {
    sinks s;
    dStepCounter obj;
    Wire(obj, s);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
    const std::string list = "2 1 3";
    const std::string seq = "seq 3 2";
    const std::string reset = "reset";
    const std::string direction = "direction 2";
    const std::string sync = "syncupdate 2";
    const std::string thresh = "thresh 0.5";
    const std::string start = "startmode 0";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(2, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.SetMessage(seq, 0.f);
      obj.SetMessage(reset, 0.f);
      obj.SetMessage(direction, 0.f);
      obj.SetMessage(sync, 0.f);
      obj.SetMessage(thresh, 0.f);
      obj.SetMessage(start, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
