// Tests for the ~rate patcher object (patcher/generatorObjects/dRate), issue
// #1073. The signal is DSP::phasorRate's and is tested in
// Tests/dsp/test_phasor_rate.cpp; these show that signals, numbers, messages
// and arguments reach it through both inlets, alone and in a rendered patch
// (a ~phasor time-scaled by ~rate, controlled through receives) after a JSON
// round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dRate.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorRate.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorRate;
using YSE::PATCHER::dRate;
using YSE::PATCHER::patcherImplementation;
using rateSync = YSE::DSP::phasorRate::syncMode;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dRate& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~rate: registry and shape (#1073)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_RATE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~rate");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_RATE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~rate: numbers, messages and arguments against the DSP class (#1073, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dRate obj;
    Wire(obj, sink);
    phasorRate reference;
    // One 128-sample input cycle per block, for the object and the reference.
    phasor objSource;
    phasor refSource;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      buffer& in = objSource(375.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(refSource(375.f, N)), 0.f));
    };

    // Defaults: multiplier 1, sync off.
    step();
    CHECK(sink.received->getPtr()[N - 1] == doctest::Approx(127.f / 128.f));

    // The right inlet's number is the multiplier; the left inlet's jumps.
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    reference.multiplier(0.5f);
    step();
    obj.GetInlet(0)->SetFloat(0.3f, YSE::T_GUI);
    reference.jump(0.3f);
    step();
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.3f));
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    reference.multiplier(2.f).jump(0.f);
    step();
    CHECK(sink.received->getPtr()[0] == 0.f);

    // Messages.
    obj.SetMessage("goto 0.9 0.5", 0.f);
    reference.jump(0.9f, 0.5f);
    step();
    CHECK(sink.received->getPtr()[64] == doctest::Approx(0.9f));
    obj.SetMessage("sync lock", 0.f);
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    reference.sync(rateSync::lock).multiplier(0.25f);
    step();
    obj.SetMessage("sync 0", 0.f);
    obj.SetMessage("goto 0.6", 0.f);
    reference.sync(rateSync::cycle).jump(0.6f);
    step();
    obj.SetMessage("oneshot 1", 0.f);
    reference.oneshot(true);
    step();
    step();
    obj.SetMessage("reset", 0.f);
    obj.SetMessage("oneshot 0", 0.f);
    reference.reset().oneshot(false);
    step();
    obj.SetMessage("sync off", 0.f);
    obj.SetMessage("sync 1", 0.f);
    obj.SetMessage("sync cycle", 0.f);
    reference.sync(rateSync::cycle);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("goto", 0.f);
    obj.SetMessage("goto x", 0.f);
    obj.SetMessage("goto 0.5 y", 0.f);
    obj.SetMessage("sync 7", 0.f);
    obj.SetMessage("sync fast", 0.f);
    obj.SetMessage("oneshot", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Arguments: multiplier sync oneshot.
    BufferSink argSink;
    dRate args;
    Wire(args, argSink);
    args.SetParams("0.25 1 1");
    phasorRate argReference;
    argReference.multiplier(0.25f).sync(rateSync::lock).oneshot(true);
    phasor argSource;
    phasor argRefSource;
    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      args.GetInlet(0)->SetBuffer(&argSource(375.f, N), YSE::T_GUI);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argSink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argSink.received, argReference(argRefSource(375.f, N)),
                                            0.f));
    }
    // One 32-sample ramp, then it holds.
    CHECK(argSink.received->getPtr()[N - 1] == 1.f);
  }

  TEST_CASE("~rate: a signal in the multiplier inlet wins over its number (#1073, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dRate obj;
    Wire(obj, sink);
    obj.SetParams("3");
    phasorRate reference;
    reference.multiplier(3.f);
    phasor objSource;
    phasor refSource;
    buffer multiplier(N);
    for (unsigned int block = 0; block < 4; ++block) {
      CAPTURE(block);
      multiplier = block < 2 ? 0.5f : 0.25f;
      obj.GetInlet(0)->SetBuffer(&objSource(375.f, N), YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&multiplier, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received,
                                            reference(refSource(375.f, N), multiplier), 0.f));
    }
    // Ran on the signal: 64 then 32 samples per ramp, not 384.
    CHECK(sink.received->getPtr()[1] - sink.received->getPtr()[0] == doctest::Approx(1.f / 32.f));
  }

  TEST_CASE("~rate: in a rendered patch, a ~phasor time-scaled by ~rate, controlled through "
            "receives, after a JSON round trip (#1073)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* scale = src.CreateObject(YSE::OBJ::D_RATE, "0.25 2 0");
      YSE::pHandle* mult = src.CreateObject(YSE::OBJ::G_RECEIVE, "mult1073");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1073");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(scale != nullptr);
      REQUIRE(mult != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, scale, 0);
      src.Connect(cmd, 0, scale, 0);
      src.Connect(mult, 0, scale, 1);
      src.Connect(scale, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorRate reference;
    reference.multiplier(0.25f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(2.f, "mult1073", YSE::T_GUI));
        reference.multiplier(2.f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("sync lock", "cmd1073", YSE::T_GUI));
        REQUIRE(dst.PassData(0.5f, "mult1073", YSE::T_GUI));
        reference.sync(rateSync::lock).multiplier(0.5f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("goto 0.75", "cmd1073", YSE::T_GUI));
        reference.jump(0.75f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      if (block == 8) CHECK(dst.output[0].getPtr()[0] == 0.75f);
    }
  }

  TEST_CASE("~rate: numbers, messages and blocks on the audio thread allocate nothing (#1073)") {
    BufferSink sink;
    dRate obj;
    Wire(obj, sink);
    buffer in(N);
    in = 0.5f;
    const std::string jump = "goto 0.5 0.25";
    const std::string mode = "sync lock";
    const std::string reset = "reset";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.2f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(2, YSE::T_DSP);
      obj.SetMessage(jump, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(reset, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
