// Tests for the ~peakamp patcher object (patcher/analysis/dPeakamp), issue
// #1126. The peaks are DSP::peakTracker's and are tested in
// Tests/dsp/test_peak_tracker.cpp; these show that the signal, the bang, the
// numbers, the messages and the arguments reach it, that the peak goes out
// only through the armed outlet (#1037), and that it works in a rendered
// ~phasor -> ~- -> ~peakamp -> .f patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dPeakamp.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dPeakamp;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Quarter = N / 4;
  constexpr unsigned int Half = N / 2;

  // ~peakamp with a float sink on its outlet. Block() runs one block and the
  // flush the patcher runs after its traversal, and says whether a number
  // went out.
  struct Rig {
    FloatSink out;
    dPeakamp peak;

    Rig() {
      Wire(peak, 0, out);
    }
    // One block with nothing connected: silence.
    bool Block() {
      out.gotFloat = false;
      peak.ResetDSP();
      peak.Calculate(YSE::T_DSP);
      peak.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
    }
    bool Block(buffer& in) {
      out.gotFloat = false;
      peak.ResetDSP();
      peak.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      peak.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
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

  TEST_CASE("~peakamp: registry and shape (#1126)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PEAKAMP);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~peakamp");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PEAKAMP));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 2);
  }

  TEST_CASE("~peakamp: a bang reports the peak so far at the next block, and starts over "
            "(#1126, #1037)") {
    Rig rig;
    buffer in(N);
    Spot(in, Quarter, -0.75f);
    CHECK_FALSE(rig.Block(in)); // no interval: nothing out
    Spot(in, Half, 0.5f);
    CHECK_FALSE(rig.Block(in));

    rig.peak.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.out.gotFloat); // a bang only asks
    Spot(in, 0, 2.f); // this block is the next window's
    rig.peak.ResetDSP();
    rig.peak.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.out.gotFloat); // CALC() only arms
    CHECK(rig.peak.GetOutlet(0)->Armed());
    rig.peak.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(rig.out.gotFloat);
    CHECK(rig.out.received == 0.75f);

    rig.peak.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block()); // unconnected: silence, and the report of the last block
    CHECK(rig.out.received == 2.f);
    rig.peak.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.f); // nothing since
  }

  TEST_CASE("~peakamp: the interval argument reports every window; a number changes it "
            "(#1126, #1037)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    Rig rig;
    rig.peak.SetParams("200"); // 200 samples
    buffer in(N);
    Spot(in, Quarter, 0.25f);
    CHECK_FALSE(rig.Block(in));
    Spot(in, N - 1, -0.5f); // sample 2N - 1, after the report at 199
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.25f);
    in = 0.f;
    CHECK_FALSE(rig.Block(in));
    REQUIRE(rig.Block(in)); // report at 399: the window from 200
    CHECK(rig.out.received == 0.5f);

    // A number in the inlet is the interval, counted from the next block,
    // and not saved.
    rig.peak.GetInlet(0)->SetInt(Half, YSE::T_GUI);
    CHECK(rig.peak.GetParams() == "200");
    Spot(in, Half, 0.125f);
    REQUIRE(rig.Block(in)); // at Half - 1 (0), then N - 1 (0.125): the last
    CHECK(rig.out.received == 0.125f);
    Spot(in, Quarter, 0.125f);
    REQUIRE(rig.Block(in)); // 0.125, then 0
    CHECK(rig.out.received == 0.f);
    in = 0.f;
    rig.peak.GetInlet(0)->SetFloat(0.f, YSE::T_GUI); // 0 stops it
    CHECK_FALSE(rig.Block(in));
    CHECK_FALSE(rig.Block(in));
    rig.peak.GetInlet(0)->SetFloat(-5.f, YSE::T_GUI); // negative is 0
    CHECK_FALSE(rig.Block(in));
    rig.peak.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    REQUIRE(rig.Block(in)); // every sample
  }

  TEST_CASE("~peakamp: 'signed' and the second argument keep the sign; 'interval' sets it "
            "(#1126)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.peak.SetParams("0 1");
    buffer in(N);
    Spot(in, Quarter, -0.75f);
    in.getPtr()[Half] = 0.5f;
    rig.Block(in);
    rig.peak.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == -0.75f);

    rig.peak.GetInlet(0)->SetMessage("signed 0", YSE::T_GUI);
    rig.Block(in);
    rig.peak.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.75f);

    rig.peak.SetMessage("signed 2", 0.f); // any non-zero is on
    rig.peak.SetMessage("interval 64", 0.f);
    Spot(in, 10, -0.25f);
    in.getPtr()[Half + 3] = 0.375f;
    REQUIRE(rig.Block(in)); // reports at 63 and 127: the last
    CHECK(rig.out.received == 0.375f);

    // Malformed or unknown messages change nothing.
    rig.peak.SetMessage("interval", 0.f);
    rig.peak.SetMessage("interval abc", 0.f);
    rig.peak.SetMessage("interval nan", 0.f);
    rig.peak.SetMessage("signed", 0.f);
    rig.peak.SetMessage("set 0", 0.f);
    rig.peak.SetMessage("", 0.f);
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.375f);
  }

  TEST_CASE("~peakamp: an unconnected inlet is silence; a signal is measured (#1126, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.peak.SetParams("1");
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.f);
    buffer in(N);
    in = -0.5f;
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.5f);
  }

  TEST_CASE("~peakamp: the arguments survive a DumpJSON / ParseJSON round trip (#1126)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_PEAKAMP, "12.5 1");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_PEAKAMP, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~peakamp");
    CHECK(a->GetParams() == "12.5 1");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~peakamp: in a rendered patch, a centred ~phasor peaks at 0.5, after a JSON round "
            "trip (#1126)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int absID = 0;
    unsigned int signedID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, from -0.5 up to just below 0.5.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* centre = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "0.5");
      // 8 ms is 384 samples: a report every three blocks
      YSE::pHandle* absPeak = src.CreateObject(YSE::OBJ::D_PEAKAMP, "8");
      YSE::pHandle* signedPeak = src.CreateObject(YSE::OBJ::D_PEAKAMP, "8 1");
      YSE::pHandle* absOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* signedOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(centre != nullptr);
      REQUIRE(absPeak != nullptr);
      REQUIRE(signedPeak != nullptr);
      REQUIRE(absOut != nullptr);
      REQUIRE(signedOut != nullptr);
      src.Connect(ramp, 0, centre, 0);
      src.Connect(centre, 0, absPeak, 0);
      src.Connect(centre, 0, signedPeak, 0);
      src.Connect(absPeak, 0, absOut, 0);
      src.Connect(signedPeak, 0, signedOut, 0);
      absID = absOut->GetID();
      signedID = signedOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* absOut = dst.GetHandleFromID(absID);
    YSE::pHandle* signedOut = dst.GetHandleFromID(signedID);
    REQUIRE(absOut != nullptr);
    REQUIRE(signedOut != nullptr);

    dst.Calculate(YSE::T_DSP);
    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(signedOut) == 0.f); // no report yet
    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(absOut) == doctest::Approx(0.5f).epsilon(1e-3));
    CHECK(GuiFloat(signedOut) == doctest::Approx(-0.5f).epsilon(1e-3));
    for (int i = 0; i < 6; ++i)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(absOut) == doctest::Approx(0.5f).epsilon(1e-3));
    CHECK(GuiFloat(signedOut) == doctest::Approx(-0.5f).epsilon(1e-3));
  }

  TEST_CASE("~peakamp: numbers, messages, a bang, blocks and the flush on the audio thread "
            "allocate nothing (#1126)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer in(N);
    Spot(in, Half, 0.5f);
    const std::string signedOn = "signed 1";
    const std::string every = "interval 16";
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.peak.GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
      rig.peak.GetInlet(0)->SetInt(4, YSE::T_DSP);
      rig.peak.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.peak.GetInlet(0)->SetMessage(signedOn, YSE::T_DSP);
      rig.peak.GetInlet(0)->SetMessage(every, YSE::T_DSP);
      rig.Block(in);
      rig.Block(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.out.received == 0.f); // the last window, 112-127, is silent
  }

} // TEST_SUITE("patcher")
