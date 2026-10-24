// Tests for the ~updown patcher object (patcher/generatorObjects/dUpdown),
// issue #1070. The signal is DSP::updown's and is tested in
// Tests/dsp/test_updown.cpp; these show that numbers, signals, bangs,
// messages and arguments reach it, alone and in a rendered patch (a ~phasor
// shaped into a trapezoid, controlled through a receive) after a JSON round
// trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dUpdown.h"
#include "dsp/generators/updown.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::updown;
using YSE::PATCHER::dUpdown;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dUpdown& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void FillRamp(buffer& b, unsigned int start, unsigned int period) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>((start + i) % period) / static_cast<float>(period);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~updown: registry and shape (#1070)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_UPDOWN);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~updown");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_UPDOWN));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);
  }

  TEST_CASE("~updown: a number as a gate, bang, messages and arguments against the DSP class "
            "(#1070, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dUpdown obj;
    BufferSink sink;
    Wire(obj, sink);
    updown reference;
    buffer gate(YSE::STANDARD_BUFFERSIZE);

    const auto step = [&](float value) {
      gate = value;
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(gate), 0.f));
    };

    // Defaults: input 0, silence.
    step(0.f);
    // A float opens the gate; an int keeps it open.
    obj.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    step(1.f);
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    step(2.f);
    step(2.f);
    step(2.f); // 4 blocks: past the default 10 ms rise (480 samples)
    CHECK(sink.received->getPtr()[YSE::STANDARD_BUFFERSIZE - 1] == 1.f);

    // Messages set the attributes.
    obj.SetMessage("level 0.5", 0.f);
    obj.SetMessage("up 1", 0.f);
    obj.SetMessage("down 4", 0.f);
    obj.SetMessage("reset 0.25", 0.f);
    reference.level(0.5f).up(1.f).down(4.f).resetThreshold(0.25f);
    step(2.f);
    CHECK(sink.received->getPtr()[0] == 0.5f);
    // Closing the gate falls over 4 ms.
    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    step(0.f);
    CHECK(sink.received->getPtr()[YSE::STANDARD_BUFFERSIZE - 1] > 0.f);

    // bang restarts the rise from 0.
    obj.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    step(1.f);
    step(1.f);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.restart();
    step(1.f);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.5f / 48.f));

    // Malformed or unknown messages: ignored.
    obj.SetMessage("up", 0.f);
    obj.SetMessage("down x", 0.f);
    obj.SetMessage("height 3", 0.f);
    step(1.f);

    // Arguments: up down level reset.
    dUpdown args;
    BufferSink argSink;
    Wire(args, argSink);
    args.SetParams("2 1 0.8 0.3");
    updown argReference;
    argReference.up(2.f).down(1.f).level(0.8f).resetThreshold(0.3f);
    args.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    for (int block = 0; block < 2; ++block) {
      args.Calculate(YSE::T_DSP);
      REQUIRE(argSink.received != nullptr);
      gate = 1.f;
      CHECK(TestHelpers::buffersNearlyEqual(*argSink.received, argReference(gate), 0.f));
    }
  }

  TEST_CASE("~updown: a ramp signal against the DSP class (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dUpdown obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.SetParams("1 1");
    // A signal wins over the number.
    obj.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    updown reference;
    reference.up(1.f).down(1.f);
    buffer ramp(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int block = 0; block < 12; ++block) {
      CAPTURE(block);
      FillRamp(ramp, block * YSE::STANDARD_BUFFERSIZE, 512);
      obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(ramp), 0.f));
    }
    // The last sample of a 512-sample cycle is 0, the middle is the top.
    CHECK(sink.received->getPtr()[YSE::STANDARD_BUFFERSIZE - 1] ==
          doctest::Approx(0.f).epsilon(1e-5));
  }

  TEST_CASE("~updown: in a rendered patch, a ~phasor shaped into a trapezoid, controlled "
            "through a receive, after a JSON round trip (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "93.75");
      YSE::pHandle* env = src.CreateObject(YSE::OBJ::D_UPDOWN, "2 1 0.8 0.5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1070");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(env != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, env, 0);
      src.Connect(cmd, 0, env, 0);
      src.Connect(env, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor rampReference;
    updown reference;
    reference.up(2.f).down(1.f).level(0.8f);
    float peak = 0.f;
    for (int block = 0; block < 24; ++block) {
      CAPTURE(block);
      if (block == 9) {
        REQUIRE(dst.PassData("level 0.4", "cmd1070", YSE::T_GUI));
        REQUIRE(dst.PassData("down 3", "cmd1070", YSE::T_GUI));
        reference.level(0.4f).down(3.f);
      }
      if (block == 15) {
        REQUIRE(dst.PassData("bang", "cmd1070", YSE::T_GUI));
        reference.restart();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = rampReference(93.75f);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(phase), 0.f));
      for (unsigned int i = 0; i < dst.output[0].getLength(); ++i)
        peak = std::max(peak, dst.output[0].getPtr()[i]);
    }
    CHECK(peak == doctest::Approx(0.8f));
  }

  TEST_CASE("~updown: numbers, bangs, messages and blocks on the audio thread allocate nothing "
            "(#1070)") {
    dUpdown obj;
    BufferSink sink;
    Wire(obj, sink);
    buffer ramp(YSE::STANDARD_BUFFERSIZE);
    FillRamp(ramp, 0, 512);
    const std::string up = "up 3";
    const std::string level = "level 0.5";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      obj.SetMessage(up, 0.f);
      obj.SetMessage(level, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
