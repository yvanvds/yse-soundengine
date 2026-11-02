// Tests for the ~avg patcher object (patcher/analysis/dAvg), issue #1119.
// The averages are DSP::movingAverage's accumulation and are tested in
// Tests/dsp/test_moving_average.cpp; these show that the signal and the bang
// reach it, that the report goes out only through the armed outlet (#1037),
// and that it works in a rendered ~phasor -> ~- -> ~avg -> .f patch, banged
// through a receive, after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dAvg.h"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dAvg;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~avg with a float sink on its outlet. Block() runs one block and the
  // flush the patcher runs after its traversal, and says whether a number
  // went out.
  struct Rig {
    FloatSink out;
    dAvg avg;

    Rig() {
      Wire(avg, 0, out);
    }
    // One block with nothing connected: silence.
    bool Block() {
      out.gotFloat = false;
      avg.ResetDSP();
      avg.Calculate(YSE::T_DSP);
      avg.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
    }
    bool Block(buffer& in) {
      out.gotFloat = false;
      avg.ResetDSP();
      avg.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      avg.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
    }
    void Bang() {
      avg.GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~avg: registry and shape (#1119)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_AVG);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~avg");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_AVG));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~avg: a bang reports the mean absolute value so far at the next block, and starts "
            "over (#1119, #1037)") {
    Rig rig;
    buffer in(N);
    in = -0.5f;
    CHECK_FALSE(rig.Block(in)); // no bang: nothing out
    in = 0.25f;
    CHECK_FALSE(rig.Block(in));

    rig.Bang();
    CHECK_FALSE(rig.out.gotFloat); // a bang only asks
    in = 2.f; // this block is the next report's
    rig.avg.ResetDSP();
    rig.avg.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.out.gotFloat); // CALC() only arms
    CHECK(rig.avg.GetOutlet(0)->Armed());
    rig.avg.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(rig.out.gotFloat);
    CHECK(rig.out.received == 0.375f); // (0.5 + 0.25) / 2, absolute values

    rig.Bang();
    REQUIRE(rig.Block()); // unconnected: silence, and the report of the last block
    CHECK(rig.out.received == 2.f);
    rig.Bang();
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.f); // one block of silence since
  }

  TEST_CASE("~avg: a bang before any block reports 0; mixed signs average their magnitudes "
            "(#1119)") {
    Rig rig;
    rig.Bang();
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.f); // nothing measured yet

    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = (i % 2 == 0) ? 0.75f : -0.25f;
    rig.Block(in);
    rig.Bang();
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.25f); // the silent block above counts: (0 + 0.5) / 2
    rig.Block(in);
    rig.Bang();
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.5f); // two blocks of magnitudes 0.75 and 0.25
  }

  TEST_CASE("~avg: survives a DumpJSON / ParseJSON round trip (#1119)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_AVG, "");
      REQUIRE(a != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    REQUIRE(a != nullptr);
    CHECK(std::string(a->Type()) == "~avg");
    CHECK(a->GetParams() == "");
  }

  TEST_CASE("~avg: in a rendered patch, a centred ~phasor averages 0.25 between bangs from a "
            "receive, after a JSON round trip (#1119)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int outID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, from -0.5 up to just below 0.5.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* centre = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "0.5");
      YSE::pHandle* avg = src.CreateObject(YSE::OBJ::D_AVG, "");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "avg1119");
      YSE::pHandle* out = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(centre != nullptr);
      REQUIRE(avg != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(out != nullptr);
      src.Connect(ramp, 0, centre, 0);
      src.Connect(centre, 0, avg, 0);
      src.Connect(control, 0, avg, 0);
      src.Connect(avg, 0, out, 0);
      outID = out->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* out = dst.GetHandleFromID(outID);
    REQUIRE(out != nullptr);

    // The reference: the same ramp, centred, its magnitudes summed per window.
    phasor rampRef;
    double total = 0.0;
    unsigned int samples = 0;
    auto run = [&](int blocks) {
      for (int b = 0; b < blocks; ++b) {
        dst.Calculate(YSE::T_DSP);
        buffer& r = rampRef(375.f, N);
        for (unsigned int i = 0; i < N; ++i)
          total += std::fabs(static_cast<double>(r.getPtr()[i] - 0.5f));
        samples += N;
      }
    };
    auto expected = [&]() {
      const auto mean = static_cast<float>(total / samples);
      total = 0.0;
      samples = 0;
      return mean;
    };

    run(4);
    CHECK(GuiFloat(out) == 0.f); // no bang yet
    REQUIRE(dst.PassBang("avg1119", YSE::T_GUI));
    float report = expected();
    run(1); // the bang is answered at the start of this block
    CHECK(GuiFloat(out) == doctest::Approx(report).epsilon(1e-5));
    CHECK(GuiFloat(out) == doctest::Approx(0.25f).epsilon(1e-3)); // mean |x| of a centred ramp

    run(2);
    REQUIRE(dst.PassBang("avg1119", YSE::T_GUI));
    report = expected(); // the three blocks since the last report
    run(1);
    CHECK(GuiFloat(out) == doctest::Approx(report).epsilon(1e-5));
    CHECK(GuiFloat(out) == doctest::Approx(0.25f).epsilon(1e-3));
  }

  TEST_CASE("~avg: a bang, blocks and the flush on the audio thread allocate nothing (#1119)") {
    Rig rig;
    buffer in(N);
    in = -0.5f;
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.avg.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.Block(in);
      rig.Block(in);
      rig.avg.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.out.received == 0.5f);
  }

} // TEST_SUITE("patcher")
