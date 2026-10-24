// Tests for the ~sash patcher object (patcher/filters/dSash), issue #1083.
// The recording and playback are DSP::sampleHoldMemory's and are tested in
// Tests/dsp/test_sample_hold_memory.cpp; these show that the signals,
// numbers, lists, messages and arguments reach it, alone and in a rendered
// patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dSash.h"
#include "dsp/generators/sampleHoldMemory.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleHoldMemory;
using YSE::PATCHER::dSash;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dSash& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A counter from `start`: sample i reads start + i.
  void Count(buffer& b, float start) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = start + static_cast<float>(i);
  }

  // Drives a ~sash and a reference DSP class with the same signals: a
  // counter, a 375 Hz phasor recording and a 93.75 Hz phasor advancing.
  struct Rig {
    BufferSink out;
    dSash obj;
    sampleHoldMemory reference;
    phasor objRecord, refRecord, objAdvance, refAdvance;
    buffer objIn{N};
    buffer refIn{N};
    float start = 0.f;

    Rig() {
      Wire(obj, out);
    }

    void Step() {
      obj.ResetDSP(); // as the patcher does before every block
      Count(objIn, start);
      Count(refIn, start);
      start += static_cast<float>(N);
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&objRecord(375.f, N), YSE::T_GUI);
      obj.GetInlet(2)->SetBuffer(&objAdvance(93.75f, N), YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer& r = reference(refIn, refRecord(375.f, N), refAdvance(93.75f, N));
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, r, 0.f));
    }
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~sash: registry and shape (#1083)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SASH);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~sash");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->IsDSPInput(2));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SASH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~sash: signals, numbers, lists and messages against the DSP class (#1083, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig;

    // Defaults: size 1, both levels 0.5, so with one place every recording
    // (sample 65 + 128k) shows at once.
    rig.Step();
    CHECK(rig.out.received->getPtr()[64] == 0.f);
    CHECK(rig.out.received->getPtr()[65] == 65.f);

    // size and mode 2: four places, played one per quarter of the slow ramp.
    rig.obj.SetMessage("size 4", 0.f);
    rig.obj.SetMessage("mode 2", 0.f);
    rig.reference.size(4).mode(sampleHoldMemory::SUBDIVISION);
    rig.Step();
    rig.Step();

    // Numbers in the trigger inlets are the levels, as are the messages.
    rig.obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    rig.reference.sampleLevel(0.25f);
    rig.Step();
    rig.obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.reference.sampleLevel(0.f);
    rig.Step();
    rig.obj.SetMessage("samplelevel 0.5", 0.f);
    rig.obj.SetMessage("mode 0", 0.f);
    rig.obj.GetInlet(2)->SetFloat(0.75f, YSE::T_GUI);
    rig.reference.sampleLevel(0.5f).mode(sampleHoldMemory::LEVEL).advanceLevel(0.75f);
    rig.Step();
    rig.obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    rig.obj.SetMessage("advancelevel 0.125", 0.f);
    rig.reference.advanceLevel(0.125f);
    rig.Step();

    // dir: backward, then random and urn from the same default seed.
    rig.obj.SetMessage("dir 1", 0.f);
    rig.reference.direction(sampleHoldMemory::REVERSE);
    rig.Step();
    rig.obj.SetMessage("dir 2", 0.f);
    rig.reference.direction(sampleHoldMemory::RANDOM);
    rig.Step();
    rig.obj.SetMessage("dir 3", 0.f);
    rig.reference.direction(sampleHoldMemory::URN);
    rig.Step();

    // mode 1: the slow ramp's whole value is always 0, the first place.
    rig.obj.SetMessage("mode 1", 0.f);
    rig.reference.mode(sampleHoldMemory::INDEX);
    rig.Step();

    // A list of numbers fills the memory and sets the size.
    rig.obj.SetMessage("mode 2", 0.f);
    rig.obj.SetMessage("samplelevel 2", 0.f); // stop recording over it
    rig.obj.GetInlet(0)->SetList("10 20 30", YSE::T_GUI);
    const float values[] = {10.f, 20.f, 30.f};
    rig.reference.mode(sampleHoldMemory::SUBDIVISION).sampleLevel(2.f).load(values, 3);
    for (int block = 0; block < 2; ++block) {
      rig.Step();
      CHECK(rig.reference.size() == 3);
      // Closed form: sample n of the slow ramp reads (n mod 512) / 512, the
      // counter names n, and each third of the ramp plays one value.
      const auto first = static_cast<unsigned int>(rig.start) - N;
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        const unsigned int place = ((first + i) % 512) * 3 / 512;
        CHECK(rig.out.received->getPtr()[i] == values[place]);
      }
    }

    // A size message after it, and a command that arrives as a list (text
    // through a receive).
    rig.obj.SetMessage("size 2", 0.f);
    rig.reference.size(2);
    rig.Step();
    rig.obj.GetInlet(0)->SetList("dir 0", YSE::T_GUI);
    rig.reference.direction(sampleHoldMemory::FORWARD);
    rig.Step();

    // Malformed or unknown messages, and maxsize as a message: ignored.
    rig.obj.SetMessage("dir", 0.f);
    rig.obj.SetMessage("dir 4", 0.f);
    rig.obj.SetMessage("mode -1", 0.f);
    rig.obj.SetMessage("mode 3", 0.f);
    rig.obj.SetMessage("size 0", 0.f);
    rig.obj.SetMessage("samplelevel x", 0.f);
    rig.obj.SetMessage("maxsize 4", 0.f);
    rig.obj.SetMessage("speed 3", 0.f);
    rig.obj.SetMessage("", 0.f);
    rig.obj.GetInlet(0)->SetList("x y", YSE::T_GUI);
    rig.Step();
    rig.Step();

    // A size above the capacity clamps to it.
    rig.obj.SetMessage("size 5000", 0.f);
    rig.reference.size(5000);
    CHECK(rig.reference.size() == sampleHoldMemory::DEFAULT_MAX_SIZE);
    rig.Step();

    // Unconnected: the input reads its number, the triggers 0.
    BufferSink idleOut;
    dSash idle;
    Wire(idle, idleOut);
    idle.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    phasor clock;
    idle.GetInlet(1)->SetBuffer(&clock(375.f, N), YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[64] == 0.f);
    CHECK(idleOut.received->getPtr()[65] == 0.75f);
    // Nothing connected, a loaded list still plays: place 0.
    BufferSink listOut;
    dSash listed;
    Wire(listed, listOut);
    listed.GetInlet(0)->SetList("4 5", YSE::T_GUI);
    listed.Calculate(YSE::T_DSP);
    REQUIRE(listOut.received != nullptr);
    CHECK(listOut.received->getPtr()[N - 1] == 4.f);
  }

  TEST_CASE("~sash: arguments are attribute keywords (#1083)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, sampleHoldMemory& reference) {
      CAPTURE(args);
      BufferSink out;
      dSash obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objRecord, refRecord, objAdvance, refAdvance;
      buffer objIn(N);
      buffer refIn(N);
      for (int block = 0; block < 6; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Count(objIn, static_cast<float>(block * N));
        Count(refIn, static_cast<float>(block * N));
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.GetInlet(1)->SetBuffer(&objRecord(375.f, N), YSE::T_GUI);
        obj.GetInlet(2)->SetBuffer(&objAdvance(-93.75f, N), YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(
            *out.received, reference(refIn, refRecord(375.f, N), refAdvance(-93.75f, N)), 0.f));
      }
    };

    sampleHoldMemory sub;
    sub.size(4).mode(sampleHoldMemory::SUBDIVISION);
    check("size 4 mode 2", sub);

    // maxsize applies first wherever it is, so the size clamps to it.
    sampleHoldMemory capped(8);
    capped.size(20).mode(sampleHoldMemory::SUBDIVISION);
    CHECK(capped.size() == 8);
    check("size 20 mode 2 maxsize 8", capped);

    sampleHoldMemory levels;
    levels.size(3).sampleLevel(0.25f).advanceLevel(0.75f).direction(sampleHoldMemory::REVERSE);
    check("samplelevel 0.25 advancelevel 0.75 dir 1 size 3", levels);

    sampleHoldMemory urn;
    urn.size(6).direction(sampleHoldMemory::URN).advanceLevel(0.1f);
    check("dir 3 size 6 advancelevel 0.1", urn);

    // Bare numbers, a keyword without a number, or out of range: skipped.
    sampleHoldMemory skipped;
    skipped.size(2);
    check("0.4 size 2 dir 9 mode -1 maxsize 0 samplelevel", skipped);

    sampleHoldMemory none;
    check("", none);
  }

  TEST_CASE("~sash: in a rendered patch, ~phasor clocks record and play a slower ramp, with "
            "lists and commands from receives, after a JSON round trip (#1083)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* slow = src.CreateObject(YSE::OBJ::D_PHASOR, "46.875");
      YSE::pHandle* record = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* advance = src.CreateObject(YSE::OBJ::D_PHASOR, "93.75");
      YSE::pHandle* sash = src.CreateObject(YSE::OBJ::D_SASH, "size 4 mode 2");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1083");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(slow != nullptr);
      REQUIRE(record != nullptr);
      REQUIRE(advance != nullptr);
      REQUIRE(sash != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(slow, 0, sash, 0);
      src.Connect(record, 0, sash, 1);
      src.Connect(advance, 0, sash, 2);
      src.Connect(cmd, 0, sash, 0);
      src.Connect(sash, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor slowRef;
    phasor recordRef;
    phasor advanceRef;
    sampleHoldMemory reference;
    reference.size(4).mode(sampleHoldMemory::SUBDIVISION);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData("samplelevel 2", "cmd1083", YSE::T_GUI));
        REQUIRE(dst.PassData("0.1 0.2 0.3 0.4 0.5", "cmd1083", YSE::T_GUI));
        const float values[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
        reference.sampleLevel(2.f).load(values, 5);
      }
      if (block == 7) {
        REQUIRE(dst.PassData("mode 0", "cmd1083", YSE::T_GUI));
        REQUIRE(dst.PassData("dir 1", "cmd1083", YSE::T_GUI));
        reference.mode(sampleHoldMemory::LEVEL).direction(sampleHoldMemory::REVERSE);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& slowBlock = slowRef(46.875f, N);
      CHECK(TestHelpers::buffersNearlyEqual(
          dst.output[0], reference(slowBlock, recordRef(375.f, N), advanceRef(93.75f, N)), 0.f));
      const float* out = dst.output[0].getPtr();
      const unsigned int base = static_cast<unsigned int>(block) * N;
      if (block < 4) {
        // Closed form: the input reads (n mod 1024) / 1024 and is recorded
        // at 65 + 128k into place k mod 4; place floor((n mod 512) / 128)
        // plays.
        for (unsigned int i = 0; i < N; ++i) {
          const unsigned int n = base + i;
          const unsigned int place = (n % 512) / 128;
          float expected = 0.f;
          for (unsigned int k = place; 65 + 128 * k <= n; k += 4)
            expected = static_cast<float>((65 + 128 * k) % 1024) / 1024.f;
          CAPTURE(n);
          CHECK(out[i] == doctest::Approx(expected));
        }
      } else if (block < 7) {
        // The list: five places, a fifth of the slow ramp each.
        for (unsigned int i = 0; i < N; i += 17) {
          const unsigned int place = ((base + i) % 512) * 5 / 512;
          CAPTURE(i);
          CHECK(out[i] == doctest::Approx(0.1f * static_cast<float>(place + 1)));
        }
      }
    }
  }

  TEST_CASE("~sash: numbers, lists, messages and blocks on the audio thread allocate nothing "
            "(#1083)") {
    BufferSink out;
    dSash obj;
    Wire(obj, out);
    buffer in(N);
    buffer sample(N);
    buffer advance(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      sample.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      advance.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    }
    const std::string list = "1 2 3 4 5 6";
    const std::string command = "dir 3";
    const std::string size = "size 8";
    const std::string mode = "mode 2";
    const std::string level = "samplelevel 0.3";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetList(command, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(0, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(0, YSE::T_DSP);
      obj.SetMessage(size, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(level, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&sample, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&advance, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
