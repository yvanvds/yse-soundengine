// Tests for the ~swing patcher object (patcher/generatorObjects/dSwing), issue
// #1076. The signal is DSP::phasorSwing's and is tested in
// Tests/dsp/test_phasor_swing.cpp; these show that signals, numbers, messages
// and arguments reach it, alone and in a rendered patch (a ~phasor swung by
// ~swing, controlled through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSwing.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorSwing.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorSwing;
using YSE::PATCHER::dSwing;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dSwing& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~swing: registry and shape (#1076)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SWING);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~swing");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SWING));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 2);
  }

  TEST_CASE("~swing: numbers, messages and arguments against the DSP class (#1076, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dSwing obj;
    Wire(obj, sink);
    phasorSwing reference;
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

    // Default swing 0.5: the second ramp starts halfway.
    step();
    CHECK(sink.received->getPtr()[64] == doctest::Approx(0.0));
    CHECK(sink.received->getPtr()[63] == doctest::Approx(63.0 / 64.0));

    // A number sets the swing.
    obj.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    reference.swing(0.75f);
    step();
    CHECK(sink.received->getPtr()[96] == doctest::Approx(0.0));
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI); // clamped to 0.85 by the DSP class
    reference.swing(1.f);
    step();
    CHECK(sink.received->getPtr()[109] < sink.received->getPtr()[108]);

    // Messages.
    obj.SetMessage("swing 0.25", 0.f);
    reference.swing(0.25f);
    step();
    CHECK(sink.received->getPtr()[32] == doctest::Approx(0.0));
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("swing 0.6", 0.f);
    reference.syncUpdate(true).swing(0.6f);
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(false);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("swing", 0.f);
    obj.SetMessage("swing x", 0.f);
    obj.SetMessage("syncupdate", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Arguments: swing syncupdate.
    BufferSink argSink;
    dSwing args;
    Wire(args, argSink);
    args.SetParams("0.7 1");
    phasorSwing argReference;
    argReference.swing(0.7f).syncUpdate(true);
    phasor argSource;
    phasor argRefSource;
    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      if (block == 1) {
        // With syncupdate on, it waits for the input wrap at the block start.
        args.SetMessage("swing 0.4", 0.f);
        argReference.swing(0.4f);
      }
      args.GetInlet(0)->SetBuffer(&argSource(375.f, N), YSE::T_GUI);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argSink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argSink.received, argReference(argRefSource(375.f, N)),
                                            0.f));
    }
    // Swing 0.4 landed at the wrap: the second ramp starts at sample 52.
    CHECK(argSink.received->getPtr()[52] < argSink.received->getPtr()[51]);
  }

  TEST_CASE("~swing: in a rendered patch, a ~phasor swung by ~swing, controlled through a "
            "receive, after a JSON round trip (#1076)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* swing = src.CreateObject(YSE::OBJ::D_SWING, "0.75 1");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1076");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(swing != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, swing, 0);
      src.Connect(cmd, 0, swing, 0);
      src.Connect(swing, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorSwing reference;
    reference.swing(0.75f).syncUpdate(true);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(0.3f, "cmd1076", YSE::T_GUI));
        reference.swing(0.3f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("syncupdate 0", "cmd1076", YSE::T_GUI));
        REQUIRE(dst.PassData("swing 0.6", "cmd1076", YSE::T_GUI));
        reference.syncUpdate(false).swing(0.6f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      const float* out = dst.output[0].getPtr();
      if (block < 2) CHECK(out[96] < out[95]); // saved arguments: 0.75
      if (block == 2 || block == 3) CHECK(out[39] < out[38]); // 0.3 from the next wrap
      if (block >= 4) CHECK(out[77] < out[76]); // 0.6
    }
  }

  TEST_CASE("~swing: numbers, messages and blocks on the audio thread allocate nothing (#1076)") {
    BufferSink sink;
    dSwing obj;
    Wire(obj, sink);
    buffer in(N);
    in = 0.5f;
    const std::string swing = "swing 0.3";
    const std::string sync = "syncupdate 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.2f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.SetMessage(swing, 0.f);
      obj.SetMessage(sync, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
