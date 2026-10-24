// Tests for the ~rand patcher object (patcher/generatorObjects/dRand),
// issue #1057. The signal itself is DSP::bandLimitedRandom's and is tested
// in Tests/dsp/test_band_limited_random.cpp; these show that the frequency
// argument, numbers and a signal reach it, alone and in a rendered patch.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dRand.h"
#include "dsp/generators/bandLimitedRandom.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::bandLimitedRandom;
using YSE::PATCHER::dRand;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dRand& rand, BufferSink& sink) {
    rand.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(rand.GetOutlet(0), 0);
  }

  // A value every `samples` samples at whatever rate the test binary runs.
  float Every(float samples) {
    return static_cast<float>(YSE::SAMPLERATE) / samples;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~rand: registry and shape (#1057)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_RAND);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~rand");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_RAND));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 1);
  }

  TEST_CASE("~rand: without a frequency it holds at 0 (#1057)") {
    dRand rand;
    BufferSink sink;
    Wire(rand, sink);
    rand.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::measureRms(*sink.received) == 0.f);
  }

  TEST_CASE("~rand: the argument, a float and an int set the frequency (#1057)") {
    dRand rand;
    BufferSink sink;
    Wire(rand, sink);
    rand.SetParams(std::to_string(Every(16.f)));
    bandLimitedRandom reference;
    rand.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(Every(16.f)), 0.f));

    rand.GetInlet(0)->SetFloat(Every(4.f), YSE::T_GUI);
    rand.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(Every(4.f)), 0.f));

    rand.GetInlet(0)->SetInt(0, YSE::T_GUI);
    rand.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(0.f), 0.f));
    // 0 holds: the whole block is one value.
    CHECK(sink.received->getPtr()[0] == sink.received->getPtr()[127]);

    rand.GetInlet(0)->SetInt(1000, YSE::T_GUI);
    rand.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(1000.f), 0.f));
  }

  TEST_CASE("~rand: a connected frequency signal wins over the number (#1057, #1036)") {
    dRand rand;
    BufferSink sink;
    Wire(rand, sink);
    rand.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = Every(8.f);
    bandLimitedRandom reference;
    rand.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(frequency), 0.f));
    CHECK(TestHelpers::measureRms(*sink.received) > 0.f);
  }

  TEST_CASE("~rand: in a rendered patch, driven by ~sig, after a JSON round trip (#1057)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* sig = src.CreateObject(YSE::OBJ::D_SIG, std::to_string(Every(32.f)));
      YSE::pHandle* bySignal = src.CreateObject(YSE::OBJ::D_RAND, "");
      YSE::pHandle* byArgument = src.CreateObject(YSE::OBJ::D_RAND, std::to_string(Every(16.f)));
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(sig != nullptr);
      src.Connect(sig, 0, bySignal, 0);
      src.Connect(bySignal, 0, dac, 0);
      src.Connect(byArgument, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    bandLimitedRandom signalReference;
    bandLimitedRandom argumentReference;
    for (int block = 0; block < 3; ++block) {
      dst.Calculate(YSE::T_DSP);
      CAPTURE(block);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], signalReference(Every(32.f)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], argumentReference(Every(16.f)), 0.f));
    }
    CHECK(TestHelpers::measureRms(dst.output[0]) > 0.f);
    CHECK(TestHelpers::measureRms(dst.output[1]) > 0.f);
  }

  TEST_CASE("~rand: numbers and blocks on the audio thread allocate nothing (#1057)") {
    dRand rand;
    rand.SetParams("440");
    BufferSink sink;
    Wire(rand, sink);
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 110.f;
    rand.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      rand.GetInlet(0)->SetFloat(220.f, YSE::T_DSP);
      rand.Calculate(YSE::T_DSP);
      rand.GetInlet(0)->SetInt(3, YSE::T_DSP);
      rand.Calculate(YSE::T_DSP);
      rand.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
