// Tests for the ~sah patcher object (patcher/filters/dSah), issue #1082. The
// sampling is DSP::sampleHold's and is tested in Tests/dsp/test_sample_hold.cpp;
// these show that the signals, numbers, messages and arguments reach it,
// alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dSah.h"
#include "dsp/filters.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleHold;
using YSE::PATCHER::dSah;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dSah& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A counter from `start`: sample i reads start + i.
  void Count(buffer& b, float start) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = start + static_cast<float>(i);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~sah: registry and shape (#1082)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SAH);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~sah");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SAH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~sah: signals, numbers and messages against the DSP class (#1082, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dSah obj;
    Wire(obj, out);
    sampleHold reference;
    reference.triggerMode(sampleHold::ASCENDING);
    phasor objClock;
    phasor refClock;
    buffer objIn(N);
    buffer refIn(N);
    float start = 0.f;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      Count(objIn, start);
      Count(refIn, start);
      start += static_cast<float>(N);
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&objClock(375.f, N), YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, refClock(375.f, N)),
                                            0.f));
    };

    // Default: ascending through 0, the first sample above it in each cycle.
    step();
    CHECK(out.received->getPtr()[0] == 0.f);
    CHECK(out.received->getPtr()[1] == 1.f);
    step();
    CHECK(out.received->getPtr()[5] == 129.f);

    // A number in the trigger inlet is the threshold, as is the message.
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    reference.threshold(0.5f);
    step();
    CHECK(out.received->getPtr()[70] == 256.f + 65.f);
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    reference.threshold(0.f);
    step();
    obj.SetMessage("thresh 0.25", 0.f);
    reference.threshold(0.25f);
    step();

    // triggermode: 1 falls only, which a rising ramp never does through
    // 0.25; 6 is a phase, every reset with threshold 0.
    obj.SetMessage("triggermode 1", 0.f);
    reference.triggerMode(sampleHold::DESCENDING);
    step();
    obj.SetMessage("thresh 0", 0.f);
    obj.SetMessage("triggermode 6", 0.f);
    reference.threshold(0.f).triggerMode(sampleHold::PHASE);
    step();
    CHECK(out.received->getPtr()[1] == start - static_cast<float>(N) + 1.f);

    // duration and its alias.
    obj.SetMessage("duration 4", 0.f);
    reference.duration(4);
    step();
    CHECK(out.received->getPtr()[4] == start - static_cast<float>(N) + 1.f);
    CHECK(out.received->getPtr()[5] == 0.f);
    obj.SetMessage("dur 0", 0.f);
    reference.duration(0);
    step();

    // A command that arrives as a list (text through a receive) is a command.
    obj.GetInlet(0)->SetList("triggermode 0", YSE::T_GUI);
    reference.triggerMode(sampleHold::ASCENDING);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("triggermode", 0.f);
    obj.SetMessage("triggermode 7", 0.f);
    obj.SetMessage("triggermode -1", 0.f);
    obj.SetMessage("duration -3", 0.f);
    obj.SetMessage("thresh x", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Unconnected: the input reads its number, the trigger 0, so with the
    // trigger connected the number is what is sampled.
    BufferSink idleOut;
    dSah idle;
    Wire(idle, idleOut);
    idle.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    phasor clock;
    idle.GetInlet(1)->SetBuffer(&clock(375.f, N), YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[0] == 0.f);
    CHECK(idleOut.received->getPtr()[1] == 0.75f);
    // With neither connected nothing triggers.
    BufferSink silentOut;
    dSah silent;
    Wire(silent, silentOut);
    silent.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    silent.Calculate(YSE::T_DSP);
    REQUIRE(silentOut.received != nullptr);
    CHECK(silentOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~sah: arguments are the threshold, with keywords (#1082)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, sampleHold& reference) {
      CAPTURE(args);
      BufferSink out;
      dSah obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objClock;
      phasor refClock;
      buffer objIn(N);
      buffer refIn(N);
      for (int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Count(objIn, static_cast<float>(block * N));
        Count(refIn, static_cast<float>(block * N));
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.GetInlet(1)->SetBuffer(&objClock(-375.f, N), YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, refClock(-375.f, N)),
                                              0.f));
      }
    };

    sampleHold plain;
    plain.triggerMode(sampleHold::ASCENDING).threshold(0.4f);
    check("0.4", plain);

    // The keywords anywhere; a later bare number is ignored.
    sampleHold falling;
    falling.triggerMode(sampleHold::DESCENDING).threshold(0.4f).duration(10);
    check("triggermode 1 0.4 dur 10 0.9", falling);
    sampleHold keyword;
    keyword.triggerMode(sampleHold::PHASE).threshold(0.25f).duration(3);
    check("thresh 0.25 triggermode 6 duration 3", keyword);

    // A keyword without a number, or out of range, is skipped.
    sampleHold skipped;
    skipped.triggerMode(sampleHold::ASCENDING).threshold(0.4f);
    check("0.4 triggermode 9 duration -1 thresh", skipped);

    sampleHold none;
    none.triggerMode(sampleHold::ASCENDING);
    check("", none);
  }

  TEST_CASE("~sah: in a rendered patch, a ~phasor clock sampling a slower ramp, with commands "
            "from receives, after a JSON round trip (#1082)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* slow = src.CreateObject(YSE::OBJ::D_PHASOR, "93.75");
      YSE::pHandle* clock = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* sah = src.CreateObject(YSE::OBJ::D_SAH, "0.5 duration 32");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1082");
      YSE::pHandle* th = src.CreateObject(YSE::OBJ::G_RECEIVE, "th1082");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(slow != nullptr);
      REQUIRE(clock != nullptr);
      REQUIRE(sah != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(th != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(slow, 0, sah, 0);
      src.Connect(clock, 0, sah, 1);
      src.Connect(cmd, 0, sah, 0);
      src.Connect(th, 0, sah, 1);
      src.Connect(sah, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor slowRef;
    phasor clockRef;
    sampleHold reference;
    reference.triggerMode(sampleHold::ASCENDING).threshold(0.5f).duration(32);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData("dur 0", "cmd1082", YSE::T_GUI));
        reference.duration(0);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("triggermode 6", "cmd1082", YSE::T_GUI));
        REQUIRE(dst.PassData(0.f, "th1082", YSE::T_GUI));
        reference.triggerMode(sampleHold::PHASE).threshold(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& slowBlock = slowRef(93.75f, N);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(slowBlock, clockRef(375.f, N)),
                                            0.f));
      // Closed form: the slower ramp reads (n mod 512) / 512, sampled at
      // 65 + 128k while the duration of 32 lasts.
      if (block < 3) {
        const unsigned int base = static_cast<unsigned int>(block) * N;
        CHECK(dst.output[0].getPtr()[64] == 0.f);
        CHECK(dst.output[0].getPtr()[65] ==
              doctest::Approx(static_cast<float>((base + 65) % 512) / 512.f));
        CHECK(dst.output[0].getPtr()[96] ==
              doctest::Approx(static_cast<float>((base + 65) % 512) / 512.f));
        CHECK(dst.output[0].getPtr()[97] == 0.f);
      }
    }
  }

  TEST_CASE("~sah: numbers, messages and blocks on the audio thread allocate nothing (#1082)") {
    BufferSink out;
    dSah obj;
    Wire(obj, out);
    buffer in(N);
    buffer trigger(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      trigger.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    }
    const std::string list = "triggermode 2";
    const std::string thresh = "thresh 0.3";
    const std::string mode = "triggermode 6";
    const std::string dur = "duration 5";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(0, YSE::T_DSP);
      obj.SetMessage(thresh, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(dur, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&trigger, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
