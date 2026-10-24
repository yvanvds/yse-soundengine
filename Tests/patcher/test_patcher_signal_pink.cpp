// Tests for the ~pink patcher object (patcher/generatorObjects/dPink),
// issue #1056. The noise itself is DSP::pinkNoise's and its spectrum is
// tested in Tests/dsp/test_pink_noise.cpp; these show that the wrapper hands
// that generator's blocks to its outlet, alone and in a rendered patch.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dPink.h"
#include "dsp/generators/pinkNoise.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dPink;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dPink& pink, BufferSink& sink) {
    pink.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(pink.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("pink: registry and shape (#1056)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PINK);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~pink");
    CHECK(h->GetInputs() == 0);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_PINK));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
  }

  TEST_CASE("pink: each block is the next block of DSP::pinkNoise (#1056)") {
    dPink pink;
    BufferSink sink;
    Wire(pink, sink);
    YSE::DSP::pinkNoise reference;
    for (int block = 0; block < 4; ++block) {
      pink.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(), 0.f));
    }
  }

  TEST_CASE("pink: in a rendered patch it reaches the dac, after a JSON round trip (#1056)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* pink = src.CreateObject(YSE::OBJ::D_PINK, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(pink, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::DSP::pinkNoise reference;
    for (int block = 0; block < 3; ++block) {
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(), 0.f));
    }
    CHECK(TestHelpers::measureRms(dst.output[0]) > 0.f);
  }

  TEST_CASE("pink: blocks on the audio thread allocate nothing (#1056)") {
    dPink pink;
    BufferSink sink;
    Wire(pink, sink);
    pink.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      pink.Calculate(YSE::T_DSP);
      pink.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
