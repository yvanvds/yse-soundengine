// Tests for the ~ramp patcher object (patcher/generatorObjects/dRamp), issue
// #1072. The signal is DSP::triggeredRamp's and is tested in
// Tests/dsp/test_triggered_ramp.cpp; these show that signals, numbers,
// messages and arguments reach it through all four inlets, alone and in a
// rendered patch (a ~sig stepped through a receive triggering the ramp,
// controlled through another receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dRamp.h"
#include "dsp/generators/triggeredRamp.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::triggeredRamp;
using YSE::PATCHER::dRamp;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dRamp& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A trigger block: 0 up to `at`, 1 from there.
  void Edge(buffer& b, unsigned int at) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = i < at ? 0.f : 1.f;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~ramp: registry and shape (#1072)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_RAMP);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~ramp");
    CHECK(obj->GetInputs() == 4);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_RAMP));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 7);
  }

  TEST_CASE("~ramp: numbers, messages and arguments against the DSP class (#1072, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dRamp obj;
    Wire(obj, sink);
    triggeredRamp reference;
    buffer trigger(N);
    buffer silence(N);
    silence = 0.f;

    // One block; `edge` is where the trigger rises in it, N for none (the
    // trigger inlet is then left unconnected and reads 0).
    const auto step = [&](unsigned int edge) {
      obj.ResetDSP(); // as the patcher does before every block
      if (edge < N) {
        Edge(trigger, edge);
        obj.GetInlet(0)->SetBuffer(&trigger, YSE::T_GUI);
      }
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(edge < N ? trigger : silence),
                                            0.f));
    };

    // Defaults: start (0) until a trigger, then a 100 ms ramp.
    step(N);
    CHECK(sink.received->getPtr()[0] == 0.f);
    step(10);
    CHECK(sink.received->getPtr()[11] == doctest::Approx(1.f / 4800.f));
    // The duration is latched at the trigger: a number now does not change
    // the running ramp (4800 samples, 38 blocks).
    obj.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    reference.duration(1.f);
    for (int block = 0; block < 38; ++block)
      step(N); // the trigger goes back to 0
    CHECK(sink.received->getPtr()[N - 1] == 1.f);

    // Numbers: the left and second inlets set the duration, the third the
    // start, the right the end.
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(3)->SetInt(-1, YSE::T_GUI);
    reference.duration(2.f).start(0.5f).end(-1.f);
    step(N);
    CHECK(sink.received->getPtr()[0] == -1.f); // ended: holds the (new) end
    step(0);
    CHECK(sink.received->getPtr()[48] == doctest::Approx(-0.25f));
    step(N);
    CHECK(sink.received->getPtr()[N - 1] == -1.f);

    // Messages set the attributes.
    obj.SetMessage("duration 1", 0.f);
    obj.SetMessage("start 0", 0.f);
    obj.SetMessage("end 2", 0.f);
    obj.SetMessage("curve 0.5", 0.f);
    obj.SetMessage("mode 1", 0.f);
    obj.SetMessage("reset 1", 0.f);
    obj.SetMessage("retrigger 1", 0.f);
    obj.SetMessage("interval 16", 0.f); // accepted, nothing to set
    reference.duration(1.f).start(0.f).end(2.f).curve(0.5f);
    reference.mode(triggeredRamp::durationMode::continuous).resetToZero(true).retrigger(true);
    step(N);
    CHECK(sink.received->getPtr()[0] == 0.f); // reset: 0 after the ramp
    step(0);
    step(N);
    // Retrigger: a second edge while the 48-sample ramp runs restarts it.
    Edge(trigger, 0);
    trigger.getPtr()[20] = 0.f;
    obj.ResetDSP();
    obj.GetInlet(0)->SetBuffer(&trigger, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(trigger), 0.f));
    CHECK(sink.received->getPtr()[21] == 0.f);

    // Malformed or unknown messages: ignored.
    obj.SetMessage("duration", 0.f);
    obj.SetMessage("end x", 0.f);
    obj.SetMessage("speed 3", 0.f);
    step(N);
    step(5);

    // Arguments: duration start end curve mode reset retrigger.
    BufferSink argSink;
    dRamp args;
    Wire(args, argSink);
    args.SetParams("3 1 0 -0.5 0 1 0");
    triggeredRamp argReference;
    argReference.duration(3.f).start(1.f).end(0.f).curve(-0.5f).resetToZero(true);
    for (unsigned int block = 0; block < 4; ++block) {
      CAPTURE(block);
      Edge(trigger, block == 1 ? 7 : N);
      args.GetInlet(0)->SetBuffer(&trigger, YSE::T_GUI);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argSink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argSink.received, argReference(trigger), 0.f));
    }
  }

  TEST_CASE("~ramp: signals in the duration, start and end inlets win over their numbers "
            "(#1072, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dRamp obj;
    Wire(obj, sink);
    obj.SetParams("50 9 9");
    triggeredRamp reference;
    reference.mode(triggeredRamp::durationMode::continuous);
    obj.SetMessage("mode 1", 0.f);
    buffer trigger(N);
    buffer duration(N);
    buffer start(N);
    buffer end(N);
    for (unsigned int block = 0; block < 8; ++block) {
      CAPTURE(block);
      Edge(trigger, block == 0 ? 3 : 0);
      duration = block < 1 ? 4.f : 6.f; // 192, then 288 samples, mid-ramp
      for (unsigned int i = 0; i < N; ++i) {
        start.getPtr()[i] = -0.001f * static_cast<float>(i);
        end.getPtr()[i] = 0.25f + 0.001f * static_cast<float>(block);
      }
      obj.GetInlet(0)->SetBuffer(&trigger, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&duration, YSE::T_GUI);
      obj.GetInlet(2)->SetBuffer(&start, YSE::T_GUI);
      obj.GetInlet(3)->SetBuffer(&end, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received,
                                            reference(trigger, duration, start, end), 0.f));
    }
    // The ramp ran on the 6 ms signal, not the 50 ms argument, and ended on
    // the end signal.
    CHECK(sink.received->getPtr()[N - 1] == doctest::Approx(0.257f));
  }

  TEST_CASE("~ramp: in a rendered patch, a ~sig stepped through a receive triggers the ramp, "
            "controlled through a receive, after a JSON round trip (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_SIG, "0");
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_RAMP, "2 0.2 0.8 0 0 0 0");
      YSE::pHandle* trig = src.CreateObject(YSE::OBJ::G_RECEIVE, "trig1072");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1072");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(gate != nullptr);
      REQUIRE(ramp != nullptr);
      REQUIRE(trig != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(trig, 0, gate, 0);
      src.Connect(gate, 0, ramp, 0);
      src.Connect(cmd, 0, ramp, 0);
      src.Connect(ramp, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    triggeredRamp reference;
    reference.duration(2.f).start(0.2f).end(0.8f);
    buffer trigger(N);
    float gateValue = 0.f;
    for (int block = 0; block < 16; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(1.f, "trig1072", YSE::T_GUI));
        gateValue = 1.f;
      }
      if (block == 6) {
        REQUIRE(dst.PassData(0.f, "trig1072", YSE::T_GUI));
        REQUIRE(dst.PassData("end -1", "cmd1072", YSE::T_GUI));
        REQUIRE(dst.PassData("curve 0.5", "cmd1072", YSE::T_GUI));
        reference.end(-1.f).curve(0.5f);
        gateValue = 0.f;
      }
      if (block == 9) {
        REQUIRE(dst.PassData(1.f, "trig1072", YSE::T_GUI));
        gateValue = 1.f;
      }
      dst.Calculate(YSE::T_DSP);
      trigger = gateValue;
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(trigger), 0.f));
    }
    // Two ramps ran: the second ended on the new end value.
    CHECK(dst.output[0].getPtr()[N - 1] == -1.f);
  }

  TEST_CASE("~ramp: numbers, messages and blocks on the audio thread allocate nothing (#1072)") {
    BufferSink sink;
    dRamp obj;
    Wire(obj, sink);
    buffer trigger(N);
    Edge(trigger, 10);
    const std::string duration = "duration 3";
    const std::string curve = "curve 0.5";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(2, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(3)->SetFloat(0.f, YSE::T_DSP);
      obj.SetMessage(duration, 0.f);
      obj.SetMessage(curve, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&trigger, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
