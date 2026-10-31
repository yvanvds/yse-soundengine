// Tests for the ~delta patcher object (patcher/math/dDelta), issue #1108.
// The difference is DSP::delta's and is tested in Tests/dsp/test_delta.cpp;
// these show that the signal and the numbers reach it, alone and in a
// rendered ~phasor -> ~delta -> ~dac patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dDelta.h"
#include "dsp/operators/delta.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::delta;
using YSE::DSP::phasor;
using YSE::PATCHER::dDelta;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Last = N - 1;

  void Wire(dDelta& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~delta: registry and shape (#1108)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_DELTA);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~delta");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_DELTA));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~delta: a number shows as one sample of difference, then 0 (#1108, #1036)") {
    BufferSink out;
    dDelta obj;
    Wire(obj, out);

    obj.Calculate(YSE::T_DSP); // unconnected and no number: reads 0
    REQUIRE(out.received != nullptr);
    CHECK(out.received->getPtr()[0] == 0.f);
    CHECK(out.received->getPtr()[Last] == 0.f);

    obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == 2.5f);
    CHECK(out.received->getPtr()[1] == 0.f);
    CHECK(out.received->getPtr()[Last] == 0.f);

    obj.Calculate(YSE::T_DSP); // the same number: no change across the block edge
    CHECK(out.received->getPtr()[0] == 0.f);

    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == -3.5f);
    CHECK(out.received->getPtr()[1] == 0.f);
  }

  TEST_CASE("~delta: a connected signal wins, and matches the DSP class (#1108, #1036)") {
    BufferSink out;
    dDelta obj;
    Wire(obj, out);
    obj.GetInlet(0)->SetFloat(100.f, YSE::T_GUI); // ignored while a signal is connected

    buffer in(N);
    delta reference;
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>((block * N + i) % 7u) * 0.25f;
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(in), 0.f));
    }
  }

  TEST_CASE("~delta: in a rendered patch, the slope of a ~phasor and a spike at each wrap, after "
            "a JSON round trip (#1108)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* diff = src.CreateObject(YSE::OBJ::D_DELTA, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(diff != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, diff, 0);
      src.Connect(diff, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    delta reference;
    // 375 Hz at 48 kHz: a cycle every 128 samples, a rise of 1/128 per sample.
    int wraps = 0;
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      for (unsigned int i = 0; i < N; ++i) {
        const float y = dst.output[0].getPtr()[i];
        if (y < -0.5f)
          ++wraps;
        else if (block > 0 || i > 0)
          CHECK(y == doctest::Approx(1.0 / 128.0).epsilon(1e-4));
      }
    }
    CHECK(wraps == static_cast<int>(6 * N / 128) - 1);
  }

  TEST_CASE("~delta: numbers and blocks on the audio thread allocate nothing (#1108)") {
    BufferSink out;
    dDelta obj;
    Wire(obj, out);
    buffer in(N);
    in = 0.5f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(2, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
