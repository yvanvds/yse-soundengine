// Tests for the ~triangle and ~trapezoid patcher objects
// (patcher/generatorObjects/dPhaseShapes), issue #1063. The signal is
// DSP::triangleShape's and DSP::trapezoidShape's and is tested in
// Tests/dsp/test_phase_shapes.cpp; these show that numbers, signals,
// messages and arguments reach them, alone and in a rendered patch (a
// ~phasor read by both, shaped through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dPhaseShapes.h"
#include "dsp/generators/phaseShapes.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::trapezoidShape;
using YSE::DSP::triangleShape;
using YSE::PATCHER::dTrapezoid;
using YSE::PATCHER::dTriangle;
using YSE::PATCHER::patcherImplementation;

namespace {
  template <typename T> void Wire(T& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // Every sample of `block` is `value`.
  bool Constant(buffer& block, float value) {
    if (block.getLength() != YSE::STANDARD_BUFFERSIZE) return false;
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != value) return false;
    return true;
  }

  void FillRamp(buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>(i) / static_cast<float>(b.getLength());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~triangle ~trapezoid: registry and shape (#1063)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* tri = p.CreateObject(YSE::OBJ::D_TRIANGLE);
    REQUIRE(tri != nullptr);
    CHECK(std::string(tri->Type()) == "~triangle");
    CHECK(tri->GetInputs() == 2);
    CHECK(tri->GetOutputs() == 1);
    CHECK(tri->IsDSPInput(0));
    CHECK(tri->IsDSPInput(1));
    CHECK(tri->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* trap = p.CreateObject(YSE::OBJ::D_TRAPEZOID);
    REQUIRE(trap != nullptr);
    CHECK(std::string(trap->Type()) == "~trapezoid");
    CHECK(trap->GetInputs() == 3);
    CHECK(trap->GetOutputs() == 1);
    CHECK(trap->IsDSPInput(0));
    CHECK(trap->IsDSPInput(1));
    CHECK(trap->IsDSPInput(2));
    CHECK(trap->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> a(YSE::PATCHER::Register().Get(YSE::OBJ::D_TRIANGLE));
    REQUIRE(a != nullptr);
    CHECK(a->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(a->IsDSPObject());
    CHECK(a->GetParamCount() == 3);
    std::unique_ptr<YSE::PATCHER::pObject> b(YSE::PATCHER::Register().Get(YSE::OBJ::D_TRAPEZOID));
    REQUIRE(b != nullptr);
    CHECK(b->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(b->IsDSPObject());
    CHECK(b->GetParamCount() == 5);
  }

  TEST_CASE("~triangle: numbers, arguments and lo / hi messages (#1063, #1036)") {
    dTriangle obj;
    BufferSink sink;
    Wire(obj, sink);
    // Defaults: phase 0, peak 0.5, range -1..1.
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Constant(*sink.received, -1.f));

    obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, triangleShape::process(0.25f, 0.5f)));

    obj.GetInlet(1)->SetFloat(0.2f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, triangleShape::process(0.25f, 0.2f)));

    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, triangleShape::process(2.f, 1.f)));

    obj.SetMessage("lo 0", 0.f);
    obj.SetMessage("hi 10", 0.f);
    obj.GetInlet(0)->SetFloat(0.1f, YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, triangleShape::process(0.1f, 0.5f, 0.f, 10.f)));
    // Not a number, or another word: ignored.
    obj.SetMessage("lo", 0.f);
    obj.SetMessage("hi x", 0.f);
    obj.SetMessage("low 3", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, triangleShape::process(0.1f, 0.5f, 0.f, 10.f)));

    // Arguments: peak lo hi.
    dTriangle args;
    BufferSink argSink;
    Wire(args, argSink);
    args.SetParams("0.25 2 4");
    args.GetInlet(0)->SetFloat(0.125f, YSE::T_GUI);
    args.Calculate(YSE::T_DSP);
    REQUIRE(argSink.received != nullptr);
    CHECK(Constant(*argSink.received, triangleShape::process(0.125f, 0.25f, 2.f, 4.f)));
  }

  TEST_CASE("~trapezoid: numbers, arguments and messages (#1063, #1036)") {
    dTrapezoid obj;
    BufferSink sink;
    Wire(obj, sink);
    // Defaults: phase 0, up 0.1, down 0.9, range 0..1, wrap.
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Constant(*sink.received, 0.f));

    obj.GetInlet(0)->SetFloat(0.05f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, trapezoidShape::process(0.05f, 0.1f, 0.9f)));

    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, trapezoidShape::process(0.75f, 0.5f, 0.5f)));

    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(1, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, trapezoidShape::process(0.75f, 0.f, 1.f)));

    // Wrap mode: 1.25 wraps to 0.25 by default and clips to 1 with wrapmode 0.
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(1.25f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, trapezoidShape::process(0.25f, 0.5f, 0.5f)));
    obj.SetMessage("wrapmode 0", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, 0.f));
    obj.SetMessage("wrapmode 1", 0.f);
    obj.SetMessage("lo -2", 0.f);
    obj.SetMessage("hi 2", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, trapezoidShape::process(1.25f, 0.5f, 0.5f, -2.f, 2.f)));

    // Arguments: up down lo hi wrapmode.
    dTrapezoid args;
    BufferSink argSink;
    Wire(args, argSink);
    args.SetParams("0.2 0.6 -1 3 0");
    args.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    args.Calculate(YSE::T_DSP);
    REQUIRE(argSink.received != nullptr);
    CHECK(Constant(*argSink.received, -1.f));
    args.GetInlet(0)->SetFloat(0.1f, YSE::T_GUI);
    args.Calculate(YSE::T_DSP);
    CHECK(Constant(*argSink.received, trapezoidShape::process(0.1f, 0.2f, 0.6f, -1.f, 3.f, false)));
  }

  TEST_CASE("~triangle ~trapezoid: signals on every inlet against the DSP classes (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer peak(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    for (unsigned int i = 0; i < peak.getLength(); ++i)
      peak.getPtr()[i] = 0.1f + 0.8f * phase.getPtr()[i];

    dTriangle tri;
    BufferSink triSink;
    Wire(tri, triSink);
    tri.SetParams("0.3");
    // Phase signal, peak number. T_GUI delivery, then a calculation.
    tri.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    tri.Calculate(YSE::T_DSP);
    REQUIRE(triSink.received != nullptr);
    triangleShape triReference;
    CHECK(TestHelpers::buffersNearlyEqual(*triSink.received, triReference(phase, 0.3f), 0.f));
    // A peak signal wins over the number.
    tri.GetInlet(1)->SetBuffer(&peak, YSE::T_GUI);
    tri.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    tri.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*triSink.received, triReference(phase, peak), 0.f));

    dTrapezoid trap;
    BufferSink trapSink;
    Wire(trap, trapSink);
    trap.SetParams("0.2 0.7");
    trap.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    trap.Calculate(YSE::T_DSP);
    REQUIRE(trapSink.received != nullptr);
    trapezoidShape trapReference;
    CHECK(
        TestHelpers::buffersNearlyEqual(*trapSink.received, trapReference(phase, 0.2f, 0.7f), 0.f));
    // An up signal alone: the unconnected down inlet reads its number.
    buffer down(YSE::STANDARD_BUFFERSIZE);
    down = 0.7f;
    trap.ResetDSP();
    trap.GetInlet(1)->SetBuffer(&peak, YSE::T_GUI);
    trap.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    trap.Calculate(YSE::T_DSP);
    CHECK(
        TestHelpers::buffersNearlyEqual(*trapSink.received, trapReference(phase, peak, down), 0.f));
    // Both point signals.
    buffer up(YSE::STANDARD_BUFFERSIZE);
    up = 0.05f;
    trap.ResetDSP();
    trap.GetInlet(1)->SetBuffer(&up, YSE::T_GUI);
    trap.GetInlet(2)->SetBuffer(&peak, YSE::T_GUI);
    trap.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    trap.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*trapSink.received, trapReference(phase, up, peak), 0.f));
  }

  TEST_CASE("~triangle ~trapezoid: in a rendered patch, a ~phasor read by both, shaped through "
            "receives, after a JSON round trip (#1063)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "220");
      YSE::pHandle* tri = src.CreateObject(YSE::OBJ::D_TRIANGLE, "0.25 0 2");
      YSE::pHandle* peakRecv = src.CreateObject(YSE::OBJ::G_RECEIVE, "peak1063");
      YSE::pHandle* triCmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "tricmd1063");
      YSE::pHandle* trap = src.CreateObject(YSE::OBJ::D_TRAPEZOID, "0.2 0.6 -1 1 1");
      YSE::pHandle* downSig = src.CreateObject(YSE::OBJ::D_SIG, "0.8");
      YSE::pHandle* trapCmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "trapcmd1063");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(tri != nullptr);
      REQUIRE(peakRecv != nullptr);
      REQUIRE(triCmd != nullptr);
      REQUIRE(trap != nullptr);
      REQUIRE(downSig != nullptr);
      REQUIRE(trapCmd != nullptr);
      src.Connect(ramp, 0, tri, 0);
      src.Connect(peakRecv, 0, tri, 1);
      src.Connect(triCmd, 0, tri, 0);
      src.Connect(ramp, 0, trap, 0);
      src.Connect(downSig, 0, trap, 2);
      src.Connect(trapCmd, 0, trap, 0);
      src.Connect(tri, 0, dac, 0);
      src.Connect(trap, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor rampReference;
    triangleShape triReference;
    trapezoidShape trapReference;
    triReference.range(0.f, 2.f);
    trapReference.range(-1.f, 1.f);
    buffer up(YSE::STANDARD_BUFFERSIZE);
    buffer down(YSE::STANDARD_BUFFERSIZE);
    up = 0.2f;
    down = 0.8f;
    float peak = 0.25f;
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(0.75f, "peak1063", YSE::T_GUI));
        peak = 0.75f;
      }
      if (block == 5) {
        REQUIRE(dst.PassData("hi 5", "tricmd1063", YSE::T_GUI));
        REQUIRE(dst.PassData("lo 0", "trapcmd1063", YSE::T_GUI));
        triReference.range(0.f, 5.f);
        trapReference.range(0.f, 1.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = rampReference(220.f);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], triReference(phase, peak), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], trapReference(phase, up, down), 0.f));
    }
    CHECK(TestHelpers::measureRms(dst.output[0]) > 0.1f);
    CHECK(TestHelpers::measureRms(dst.output[1]) > 0.1f);
  }

  TEST_CASE("~triangle ~trapezoid: numbers, messages and blocks on the audio thread allocate "
            "nothing (#1063)") {
    dTriangle tri;
    dTrapezoid trap;
    BufferSink triSink;
    BufferSink trapSink;
    Wire(tri, triSink);
    Wire(trap, trapSink);
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer points(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    points = 0.5f;
    const std::string lo = "lo -1";
    const std::string wrap = "wrapmode 0";
    tri.Calculate(YSE::T_DSP);
    trap.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      tri.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      tri.GetInlet(1)->SetInt(1, YSE::T_DSP);
      tri.SetMessage(lo, 0.f);
      tri.Calculate(YSE::T_DSP);
      tri.GetInlet(1)->SetBuffer(&points, YSE::T_DSP);
      tri.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
      trap.GetInlet(1)->SetFloat(0.2f, YSE::T_DSP);
      trap.GetInlet(2)->SetFloat(0.8f, YSE::T_DSP);
      trap.SetMessage(wrap, 0.f);
      trap.SetMessage(lo, 0.f);
      trap.Calculate(YSE::T_DSP);
      trap.GetInlet(2)->SetBuffer(&points, YSE::T_DSP);
      trap.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
