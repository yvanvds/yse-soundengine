// Tests for the ~twist patcher object (patcher/generatorObjects/dTwist), issue
// #1077. The signal is DSP::phasorTwist's and is tested in
// Tests/dsp/test_phasor_twist.cpp; these show that signals, numbers, messages
// and arguments reach it, alone and in a rendered patch (a ~phasor bent by
// ~twist, controlled through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dTwist.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorTwist.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorTwist;
using YSE::PATCHER::dTwist;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dTwist& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // One cycle of a ramp falling over 128 samples, starting at 0.
  void Fall(buffer& b) {
    for (unsigned int i = 0; i < N; ++i)
      b.getPtr()[i] = i % 128 == 0 ? 0.f : 1.f - static_cast<float>(i % 128) / 128.f;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~twist: registry and shape (#1077)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_TWIST);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~twist");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_TWIST));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~twist: numbers, messages and arguments against the DSP class (#1077, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dTwist obj;
    Wire(obj, sink);
    phasorTwist reference;
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

    // Default curve 0: the ramp passes unchanged.
    step();
    CHECK(sink.received->getPtr()[64] == doctest::Approx(0.5));

    // A number sets the curve.
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    reference.curve(0.5f);
    step();
    CHECK(sink.received->getPtr()[64] < 0.45f);
    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    reference.curve(-1.f);
    step();
    CHECK(sink.received->getPtr()[64] > 0.99f);

    // Messages.
    obj.SetMessage("curve 0.3", 0.f);
    reference.curve(0.3f);
    step();
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("curve -0.6", 0.f);
    reference.syncUpdate(true).curve(-0.6f);
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(false);
    step();

    // shapemode reaches the class: a falling ramp, bent both ways.
    buffer falling(N);
    Fall(falling);
    buffer refFalling(N);
    Fall(refFalling);
    obj.SetMessage("curve 0.6", 0.f);
    reference.curve(0.6f);
    for (int mode : {0, 1}) {
      CAPTURE(mode);
      obj.SetMessage(mode == 0 ? "shapemode 0" : "shapemode 1", 0.f);
      reference.shapeMode(mode != 0);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&falling, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(refFalling), 0.f));
      // On, the first falling step barely moves; off, it drops fast.
      if (mode == 1)
        CHECK(sink.received->getPtr()[1] > 1.f - 1.f / 128.f);
      else
        CHECK(sink.received->getPtr()[1] < 1.f - 1.f / 128.f);
    }

    // Malformed or unknown messages: ignored.
    obj.SetMessage("curve", 0.f);
    obj.SetMessage("curve x", 0.f);
    obj.SetMessage("shapemode", 0.f);
    obj.SetMessage("syncupdate", 0.f);
    obj.SetMessage("test", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Arguments: curve shapemode syncupdate.
    BufferSink argSink;
    dTwist args;
    Wire(args, argSink);
    args.SetParams("0.7 0 1");
    phasorTwist argReference;
    argReference.curve(0.7f).shapeMode(false).syncUpdate(true);
    phasor argSource;
    phasor argRefSource;
    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      if (block == 1) {
        // With syncupdate on, it waits for the input wrap at the block start.
        args.SetMessage("curve -0.4", 0.f);
        argReference.curve(-0.4f);
      }
      args.GetInlet(0)->SetBuffer(&argSource(375.f, N), YSE::T_GUI);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argSink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argSink.received, argReference(argRefSource(375.f, N)),
                                            0.f));
      if (block == 0) CHECK(argSink.received->getPtr()[64] < 0.4f);
      if (block >= 1) CHECK(argSink.received->getPtr()[64] > 0.6f);
    }
  }

  TEST_CASE("~twist: in a rendered patch, a ~phasor bent by ~twist, controlled through a "
            "receive, after a JSON round trip (#1077)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* twist = src.CreateObject(YSE::OBJ::D_TWIST, "0.75 1 1");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1077");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(twist != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, twist, 0);
      src.Connect(cmd, 0, twist, 0);
      src.Connect(twist, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorTwist reference;
    reference.curve(0.75f).syncUpdate(true);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(-0.5f, "cmd1077", YSE::T_GUI));
        reference.curve(-0.5f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("syncupdate 0", "cmd1077", YSE::T_GUI));
        REQUIRE(dst.PassData("curve 0", "cmd1077", YSE::T_GUI));
        reference.syncUpdate(false).curve(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      const float* out = dst.output[0].getPtr();
      if (block < 2) CHECK(out[64] < 0.4f); // saved arguments: 0.75
      if (block == 2 || block == 3) CHECK(out[64] > 0.6f); // -0.5 from the next wrap
      if (block >= 4) CHECK(out[64] == doctest::Approx(0.5)); // linear again
    }
  }

  TEST_CASE("~twist: numbers, messages and blocks on the audio thread allocate nothing (#1077)") {
    BufferSink sink;
    dTwist obj;
    Wire(obj, sink);
    buffer in(N);
    in = 0.5f;
    const std::string curve = "curve 0.3";
    const std::string shape = "shapemode 0";
    const std::string sync = "syncupdate 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.2f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.SetMessage(curve, 0.f);
      obj.SetMessage(shape, 0.f);
      obj.SetMessage(sync, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
