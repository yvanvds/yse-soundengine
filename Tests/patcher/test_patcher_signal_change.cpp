// Tests for the ~change patcher object (patcher/analysis/dChange), issue
// #1120. The direction is DSP::direction's and is tested in
// Tests/dsp/test_direction.cpp; these show that the signal and the numbers
// reach it, alone and in a rendered ~phasor -> ~change -> ~dac patch after a
// JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dChange.h"
#include "dsp/analysis/direction.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::direction;
using YSE::DSP::phasor;
using YSE::PATCHER::dChange;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Last = N - 1;

  void Wire(dChange& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~change: registry and shape (#1120)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_CHANGE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~change");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_CHANGE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~change: a new number shows as one sample of 1 or -1, then 0 (#1120, #1036)") {
    BufferSink out;
    dChange obj;
    Wire(obj, out);

    obj.Calculate(YSE::T_DSP); // unconnected and no number: reads 0
    REQUIRE(out.received != nullptr);
    CHECK(out.received->getPtr()[0] == 0.f);
    CHECK(out.received->getPtr()[Last] == 0.f);

    obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == 1.f);
    CHECK(out.received->getPtr()[1] == 0.f);
    CHECK(out.received->getPtr()[Last] == 0.f);

    obj.Calculate(YSE::T_DSP); // the same number: steady across the block edge
    CHECK(out.received->getPtr()[0] == 0.f);

    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == -1.f);
    CHECK(out.received->getPtr()[1] == 0.f);

    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI); // the same value again: no change
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == 0.f);
  }

  TEST_CASE("~change: a connected signal wins, and matches the DSP class (#1120, #1036)") {
    BufferSink out;
    dChange obj;
    Wire(obj, out);
    obj.GetInlet(0)->SetFloat(100.f, YSE::T_GUI); // ignored while a signal is connected

    buffer in(N);
    direction reference;
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

  TEST_CASE("~change: in a rendered patch, 1 along a ~phasor and -1 at each wrap, after a JSON "
            "round trip (#1120)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* dir = src.CreateObject(YSE::OBJ::D_CHANGE, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(dir != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, dir, 0);
      src.Connect(dir, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    direction reference;
    // 375 Hz at 48 kHz: a cycle every 128 samples.
    int wraps = 0;
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      for (unsigned int i = 0; i < N; ++i) {
        const float y = dst.output[0].getPtr()[i];
        if (y == -1.f)
          ++wraps;
        else if (block > 0 || i > 0)
          CHECK(y == 1.f);
      }
    }
    CHECK(wraps == static_cast<int>(6 * N / 128) - 1);
  }

  TEST_CASE("~change: numbers and blocks on the audio thread allocate nothing (#1120)") {
    BufferSink out;
    dChange obj;
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
