// Tests for the ~minmax patcher object (patcher/analysis/dMinmax), issue
// #1127. The running minimum and maximum are DSP::minMax's and are tested in
// Tests/dsp/test_min_max.cpp; these show that the signal, the numbers, the
// reset signal, the bang and 'reset' reach it, that the signals go out on
// outlets 0 and 1 and the bang's report only through the armed outlets 2
// and 3 (#1037), and that it works in a rendered ~phasor -> ~- -> ~minmax ->
// .f patch driven through a receive after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dMinmax.h"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dMinmax;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Quarter = N / 4;
  constexpr unsigned int Half = N / 2;

  // ~minmax with sinks on all four outlets. Block() runs one block and the
  // flush the patcher runs after its traversal, and says whether the bang's
  // report went out.
  struct Rig {
    BufferSink low;
    BufferSink high;
    FloatSink lowOnBang;
    FloatSink highOnBang;
    dMinmax mm;

    Rig() {
      Wire(mm, 0, low);
      Wire(mm, 1, high);
      Wire(mm, 2, lowOnBang);
      Wire(mm, 3, highOnBang);
    }
    bool Flush() {
      mm.FlushArmedOutlets(YSE::T_GUI);
      CHECK(lowOnBang.gotFloat == highOnBang.gotFloat);
      return highOnBang.gotFloat;
    }
    void Clear() {
      lowOnBang.gotFloat = false;
      highOnBang.gotFloat = false;
      low.received = nullptr;
      high.received = nullptr;
      mm.ResetDSP();
    }
    // One block with nothing connected: both inlets read their numbers.
    bool Block() {
      Clear();
      mm.Calculate(YSE::T_DSP);
      return Flush();
    }
    // One block with a signal on inlet 0 only.
    bool Block(buffer& in) {
      Clear();
      mm.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // runs CALC(), inlet 1 unconnected
      return Flush();
    }
    float Low(unsigned int i) const {
      REQUIRE(low.received != nullptr);
      return low.received->getPtr()[i];
    }
    float High(unsigned int i) const {
      REQUIRE(high.received != nullptr);
      return high.received->getPtr()[i];
    }
  };

  // A block of silence with the given value at one index.
  void Spot(buffer& b, unsigned int at, float value) {
    b = 0.f;
    b.getPtr()[at] = value;
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~minmax: registry and shape (#1127)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_MINMAX);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~minmax");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 4);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->OutputDataType(3) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_MINMAX));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~minmax: the signal outlets carry the running minimum and maximum (#1127)") {
    Rig rig;
    buffer in(N);
    Spot(in, Quarter, -0.75f);
    in.getPtr()[Half] = 0.5f;
    CHECK_FALSE(rig.Block(in)); // no bang: no number
    CHECK(rig.Low(0) == 0.f);
    CHECK(rig.High(0) == 0.f);
    CHECK(rig.Low(Quarter - 1) == 0.f);
    CHECK(rig.Low(Quarter) == -0.75f);
    CHECK(rig.High(Half - 1) == 0.f);
    CHECK(rig.High(Half) == 0.5f);
    CHECK(rig.Low(N - 1) == -0.75f);
    Spot(in, 3, 0.25f);
    rig.Block(in); // it runs on across blocks
    CHECK(rig.Low(0) == -0.75f);
    CHECK(rig.High(N - 1) == 0.5f);
  }

  TEST_CASE("~minmax: a bang reports both at the next block, without starting over "
            "(#1127, #1037)") {
    Rig rig;
    buffer in(N);
    Spot(in, Quarter, -0.75f);
    in.getPtr()[Half] = 0.5f;
    rig.Block(in);

    rig.mm.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.highOnBang.gotFloat); // a bang only asks
    Spot(in, 0, 2.f); // this block comes after the report
    rig.Clear();
    rig.mm.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.highOnBang.gotFloat); // CALC() only arms
    CHECK(rig.mm.GetOutlet(2)->Armed());
    CHECK(rig.mm.GetOutlet(3)->Armed());
    REQUIRE(rig.Flush());
    CHECK(rig.lowOnBang.received == -0.75f);
    CHECK(rig.highOnBang.received == 0.5f);

    rig.mm.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block(in));
    CHECK(rig.lowOnBang.received == -0.75f); // still running
    CHECK(rig.highOnBang.received == 2.f);
  }

  TEST_CASE("~minmax: 'reset' starts over at the next block, after a bang that came with it "
            "(#1127)") {
    Rig rig;
    buffer in(N);
    Spot(in, Quarter, -0.75f);
    rig.Block(in);

    rig.mm.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.mm.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    in = 0.25f;
    in.getPtr()[Half] = 0.375f;
    REQUIRE(rig.Block(in));
    CHECK(rig.lowOnBang.received == -0.75f); // the report comes first
    CHECK(rig.highOnBang.received == 0.f);
    CHECK(rig.Low(0) == 0.25f); // then a fresh start
    CHECK(rig.High(0) == 0.25f);
    CHECK(rig.High(N - 1) == 0.375f);

    // Unknown messages change nothing.
    rig.mm.SetMessage("set 0", 0.f);
    rig.mm.SetMessage("resets", 0.f);
    rig.mm.SetMessage("", 0.f);
    rig.mm.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block(in));
    CHECK(rig.lowOnBang.received == 0.25f);
    CHECK(rig.highOnBang.received == 0.375f);
  }

  TEST_CASE("~minmax: the right inlet resets on every non-zero sample (#1127, #1036)") {
    Rig rig;
    buffer in(N);
    buffer reset(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i);
    Spot(reset, Half, 1.f);
    rig.Clear();
    // Without cords each delivery runs CALC(): the first with inlet 0 still
    // silent (0 throughout), the second with both buffers.
    rig.mm.GetInlet(1)->SetBuffer(&reset, YSE::T_DSP);
    rig.mm.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    rig.Flush();
    CHECK(rig.Low(Half - 1) == 0.f);
    CHECK(rig.Low(Half) == static_cast<float>(Half));
    CHECK(rig.High(N - 1) == static_cast<float>(N - 1));

    // Unconnected, a number on the right holds: non-zero resets every sample.
    Rig held;
    held.mm.GetInlet(1)->SetInt(1, YSE::T_GUI);
    held.Block(in);
    CHECK(held.Low(N - 1) == static_cast<float>(N - 1));
    CHECK(held.High(N - 1) == static_cast<float>(N - 1));
    held.mm.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    held.Block(in);
    CHECK(held.Low(N - 1) == 0.f);
  }

  TEST_CASE("~minmax: an unconnected inlet reads its number, else silence (#1127, #1036)") {
    Rig rig;
    rig.Block();
    CHECK(rig.Low(0) == 0.f);
    CHECK(rig.High(0) == 0.f);
    rig.mm.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Low(0) == -0.5f);
    CHECK(rig.High(0) == 0.f);
    rig.mm.GetInlet(0)->SetInt(3, YSE::T_GUI);
    rig.Block();
    CHECK(rig.High(N - 1) == 3.f);
  }

  TEST_CASE("~minmax: no parameters, through a DumpJSON / ParseJSON round trip (#1127)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_MINMAX, "");
      REQUIRE(a != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    REQUIRE(a != nullptr);
    CHECK(std::string(a->Type()) == "~minmax");
    CHECK(a->GetParams() == "");
    CHECK(a->GetInputs() == 2);
    CHECK(a->GetOutputs() == 4);
  }

  TEST_CASE("~minmax: in a rendered patch, a centred ~phasor spans -0.5 to below 0.5, banged "
            "and reset through a receive after a JSON round trip (#1127)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int lowID = 0;
    unsigned int highID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, from -0.5 up to just below 0.5.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* centre = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "0.5");
      YSE::pHandle* mm = src.CreateObject(YSE::OBJ::D_MINMAX, "");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "mm1127");
      YSE::pHandle* lowOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* highOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(centre != nullptr);
      REQUIRE(mm != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(lowOut != nullptr);
      REQUIRE(highOut != nullptr);
      src.Connect(ramp, 0, centre, 0);
      src.Connect(centre, 0, mm, 0);
      src.Connect(control, 0, mm, 0);
      src.Connect(mm, 2, lowOut, 0);
      src.Connect(mm, 3, highOut, 0);
      lowID = lowOut->GetID();
      highID = highOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* lowOut = dst.GetHandleFromID(lowID);
    YSE::pHandle* highOut = dst.GetHandleFromID(highID);
    REQUIRE(lowOut != nullptr);
    REQUIRE(highOut != nullptr);

    // The reference: the same ramp, centred.
    phasor rampRef;
    float low = 0.f;
    float high = 0.f;
    bool any = false;
    auto run = [&](int blocks) {
      for (int b = 0; b < blocks; ++b) {
        dst.Calculate(YSE::T_DSP);
        buffer& r = rampRef(375.f, N);
        for (unsigned int i = 0; i < N; ++i) {
          const float v = r.getPtr()[i] - 0.5f;
          low = any ? std::min(low, v) : v;
          high = any ? std::max(high, v) : v;
          any = true;
        }
      }
    };

    run(3);
    CHECK(GuiFloat(lowOut) == 0.f); // no bang yet
    CHECK(GuiFloat(highOut) == 0.f);
    REQUIRE(dst.PassBang("mm1127", YSE::T_GUI));
    const float wantLow = low;
    const float wantHigh = high;
    run(1); // the bang is answered at the start of this block
    CHECK(GuiFloat(lowOut) == doctest::Approx(wantLow).epsilon(1e-6));
    CHECK(GuiFloat(highOut) == doctest::Approx(wantHigh).epsilon(1e-6));
    CHECK(GuiFloat(lowOut) == doctest::Approx(-0.5f).epsilon(1e-3));
    CHECK(GuiFloat(highOut) < 0.5f);
    CHECK(GuiFloat(highOut) > 0.45f);

    // 'reset' through the receive, then a bang before the next block: the
    // report still covers everything up to the reset.
    REQUIRE(dst.PassData(std::string("reset"), "mm1127", YSE::T_GUI));
    run(1); // the reset lands at the start of this block
    REQUIRE(dst.PassBang("mm1127", YSE::T_GUI));
    CHECK(GuiFloat(highOut) == doctest::Approx(wantHigh).epsilon(1e-6));
    run(1);
    // One full cycle since the reset spans the same range again.
    CHECK(GuiFloat(lowOut) == doctest::Approx(wantLow).epsilon(1e-6));
    CHECK(GuiFloat(highOut) == doctest::Approx(wantHigh).epsilon(1e-6));
  }

  TEST_CASE("~minmax: numbers, 'reset', a bang, blocks and the flush on the audio thread "
            "allocate nothing (#1127)") {
    Rig rig;
    buffer in(N);
    Spot(in, Half, 0.5f);
    const std::string reset = "reset";
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.mm.GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
      rig.mm.GetInlet(0)->SetInt(4, YSE::T_DSP);
      rig.mm.GetInlet(1)->SetFloat(0.f, YSE::T_DSP);
      rig.mm.GetInlet(1)->SetInt(0, YSE::T_DSP);
      rig.mm.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.mm.GetInlet(0)->SetMessage(reset, YSE::T_DSP);
      rig.Block(in);
      rig.mm.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.Block(in);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.highOnBang.received == 0.5f);
  }

} // TEST_SUITE("patcher")
