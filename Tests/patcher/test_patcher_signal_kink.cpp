// Tests for the ~kink patcher object (patcher/generatorObjects/dKink),
// issue #1060. The signal is DSP::kink's and is tested in
// Tests/dsp/test_kink.cpp; these show that numbers, signals and the slope
// argument reach it, alone and in a rendered patch (a ~phasor bent by ~kink,
// its slope set through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dKink.h"
#include "dsp/generators/kink.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::kink;
using YSE::PATCHER::dKink;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dKink& obj, BufferSink& sink) {
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

  TEST_CASE("~kink: registry and shape (#1060)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_KINK);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~kink");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->IsDSPInput(0));
    CHECK(h->IsDSPInput(1));
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_KINK));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 1);
  }

  TEST_CASE("~kink: numbers on both inlets while no signal is connected (#1060, #1036)") {
    dKink obj;
    BufferSink sink;
    Wire(obj, sink);
    // Defaults: phase 0, slope 1.
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Constant(*sink.received, 0.f));

    obj.GetInlet(0)->SetFloat(0.6f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, kink::process(0.6f, 1.f)));

    obj.GetInlet(1)->SetFloat(2.f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, kink::process(0.6f, 2.f)));

    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*sink.received, kink::process(1.f, 4.f)));
  }

  TEST_CASE("~kink: the argument is the slope and survives a JSON round trip (#1060)") {
    dKink obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.SetParams("3");
    obj.GetInlet(0)->SetFloat(0.1f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Constant(*sink.received, kink::process(0.1f, 3.f)));

    // The argument is saved and restored with the patch.
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* recv = src.CreateObject(YSE::OBJ::G_RECEIVE, "phase1060");
      YSE::pHandle* k = src.CreateObject(YSE::OBJ::D_KINK, "2.5");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(k != nullptr);
      src.Connect(recv, 0, k, 0);
      src.Connect(k, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    REQUIRE(dst.PassData(0.1f, "phase1060", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(Constant(dst.output[0], kink::process(0.1f, 2.5f)));
  }

  TEST_CASE("~kink: signals on both inlets against DSP::kink (#1060)") {
    dKink obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.SetParams("2");
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);

    // Phase signal, slope number. T_GUI delivery on the cold inlet, then a
    // calculation, as a T_DSP SetBuffer on inlet 0 calculates at once.
    obj.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    kink reference;
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(phase, 2.f), 0.f));

    // A slope signal wins over the number.
    buffer slope(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < slope.getLength(); ++i)
      slope.getPtr()[i] = 0.5f + 0.04f * static_cast<float>(i);
    obj.GetInlet(1)->SetBuffer(&slope, YSE::T_GUI);
    obj.GetInlet(0)->SetBuffer(&phase, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(phase, slope), 0.f));
  }

  TEST_CASE("~kink: in a rendered patch, a ~phasor bent with its slope set through a receive, "
            "after a JSON round trip (#1060)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "110");
      YSE::pHandle* bent = src.CreateObject(YSE::OBJ::D_KINK, "3");
      YSE::pHandle* recv = src.CreateObject(YSE::OBJ::G_RECEIVE, "slope1060");
      YSE::pHandle* slopeSig = src.CreateObject(YSE::OBJ::D_SIG, "0.75");
      YSE::pHandle* modulated = src.CreateObject(YSE::OBJ::D_KINK, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(bent != nullptr);
      REQUIRE(recv != nullptr);
      REQUIRE(slopeSig != nullptr);
      REQUIRE(modulated != nullptr);
      src.Connect(ramp, 0, bent, 0);
      src.Connect(recv, 0, bent, 1);
      src.Connect(ramp, 0, modulated, 0);
      src.Connect(slopeSig, 0, modulated, 1);
      src.Connect(bent, 0, dac, 0);
      src.Connect(modulated, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor rampReference;
    kink bentReference;
    kink modulatedReference;
    buffer slope(YSE::STANDARD_BUFFERSIZE);
    slope = 0.75f;
    float s = 3.f;
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData(1.5f, "slope1060", YSE::T_GUI));
        s = 1.5f;
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = rampReference(110.f);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], bentReference(phase, s), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], modulatedReference(phase, slope), 0.f));
    }
    CHECK(TestHelpers::measureRms(dst.output[0]) > 0.1f);
  }

  TEST_CASE("~kink: numbers and blocks on the audio thread allocate nothing (#1060)") {
    dKink obj;
    BufferSink sink;
    Wire(obj, sink);
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer slope(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    slope = 2.f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(2.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(3, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&slope, YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
