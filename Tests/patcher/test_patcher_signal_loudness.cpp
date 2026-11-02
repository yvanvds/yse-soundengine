// Tests for the ~loudness patcher object (patcher/analysis/dLoudness), issue
// #1129, with the loudness range and true peak outlets of #1237. The
// measurement is DSP::loudnessMeter's and is tested in
// Tests/dsp/test_loudness_meter.cpp; these show that the signals, the bang,
// the numbers and the messages reach it, that the reports go out only
// through the armed outlets (#1037), that the channel count shapes the
// inlets and survives a JSON round trip, and that a rendered
// ~cycle -> ~* -> ~loudness -> .f patch reads -23 LUFS for a 1 kHz sine at
// -20 dBFS.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dLoudness.h"
#include "dsp/analysis/loudnessMeter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::loudnessMeter;
using YSE::PATCHER::dLoudness;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double Pi = 3.14159265358979323846;

  // ~loudness (one channel) with sinks on all six outlets. Block() runs one
  // block and the flush the patcher runs after its traversal, and says
  // whether a report went out.
  struct Rig {
    FloatSink momentary;
    FloatSink shortTerm;
    FloatSink integrated;
    FloatSink range;
    FloatSink peak;
    FloatSink truePeak;
    dLoudness ld;

    Rig() {
      Wire(ld, 0, momentary);
      Wire(ld, 1, shortTerm);
      Wire(ld, 2, integrated);
      Wire(ld, 3, range);
      Wire(ld, 4, peak);
      Wire(ld, 5, truePeak);
    }
    bool Block(buffer& in) {
      momentary.gotFloat = false;
      shortTerm.gotFloat = false;
      integrated.gotFloat = false;
      range.gotFloat = false;
      peak.gotFloat = false;
      truePeak.gotFloat = false;
      ld.ResetDSP();
      ld.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // runs CALC()
      ld.FlushArmedOutlets(YSE::T_GUI);
      CHECK(momentary.gotFloat == peak.gotFloat);
      CHECK(shortTerm.gotFloat == peak.gotFloat);
      CHECK(integrated.gotFloat == peak.gotFloat);
      CHECK(range.gotFloat == peak.gotFloat);
      CHECK(truePeak.gotFloat == peak.gotFloat);
      return peak.gotFloat;
    }
  };

  // Block k of a 1 kHz sine at `amp` and 48 kHz.
  void Sine(buffer& b, int k, double amp) {
    for (unsigned int i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(
          amp * std::sin(2.0 * Pi * 1000.0 * static_cast<double>(k * N + i) / 48000.0));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~loudness: registry and shape (#1129)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_LOUDNESS);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~loudness");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 6);
    CHECK(obj->IsDSPInput(0));
    for (int i = 0; i < 6; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_LOUDNESS));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~loudness: a bang reports what DSP::loudnessMeter measures, at the next block "
            "(#1129, #1037)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    loudnessMeter ref;
    buffer in(N);
    int k = 0;
    for (; k < 1688; ++k) { // 2 s at -20 dBFS, then 2.5 s at -30
      Sine(in, k, k < 750 ? 0.1 : 0.0316);
      CHECK_FALSE(rig.Block(in)); // no bang, no interval: no report
      ref(in);
    }
    rig.ld.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.peak.gotFloat); // a bang only asks
    const float m = ref.momentary();
    const float s = ref.shortTerm();
    const float i = ref.integrated();
    const float pk = ref.peak();
    const float lra = ref.loudnessRange();
    const float tp = ref.truePeak();
    Sine(in, k, 0.5); // this block comes after the report
    REQUIRE(rig.Block(in));
    CHECK(rig.momentary.received == m);
    CHECK(rig.shortTerm.received == s);
    CHECK(rig.integrated.received == i);
    CHECK(rig.peak.received == pk);
    CHECK(rig.range.received == lra);
    CHECK(rig.truePeak.received == tp);
    CHECK(std::fabs(m - -33.f) <= 0.1f);
    CHECK(pk == doctest::Approx(-20.f).epsilon(1e-3));
    CHECK(tp >= pk);
    CHECK(lra > 1.f); // the step from -20 to -30 dBFS spreads the short-term loudness
  }

  TEST_CASE("~loudness: a number picks the peak channel and reports; 'channel' only picks "
            "(#1129, #1237)") {
    ScopedSampleRate rate(48000);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_LOUDNESS, "2");
    REQUIRE(h != nullptr);
    CHECK(h->GetInputs() == 2);

    // Driven directly: two inlets, each delivery runs CALC() with the
    // other reading silence, so one block of each is enough for the peaks.
    dLoudness two;
    two.SetParams("2");
    REQUIRE(two.GetInlet(1) != nullptr);
    FloatSink peak;
    FloatSink truePeak;
    FloatSink other[4];
    Wire(two, 0, other[0]);
    Wire(two, 1, other[1]);
    Wire(two, 2, other[2]);
    Wire(two, 3, other[3]);
    Wire(two, 4, peak);
    Wire(two, 5, truePeak);
    buffer left(N);
    buffer right(N);
    left = 0.25f;
    right = 0.5f;
    two.ResetDSP();
    two.GetInlet(0)->SetBuffer(&left, YSE::T_DSP);
    two.GetInlet(1)->SetBuffer(&right, YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    CHECK_FALSE(peak.gotFloat);

    two.GetInlet(0)->SetInt(1, YSE::T_GUI);
    two.ResetDSP();
    two.Calculate(YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(peak.gotFloat);
    CHECK(peak.received == doctest::Approx(20.f * std::log10(0.25f)).epsilon(1e-4));
    REQUIRE(truePeak.gotFloat);
    const float leftTruePeak = truePeak.received;
    CHECK(leftTruePeak >= peak.received);

    two.GetInlet(0)->SetFloat(2.7f, YSE::T_GUI); // converted to int: 2
    peak.gotFloat = false;
    two.ResetDSP();
    two.Calculate(YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(peak.gotFloat);
    CHECK(peak.received == doctest::Approx(20.f * std::log10(0.5f)).epsilon(1e-4));
    CHECK(truePeak.received >= peak.received);
    CHECK(truePeak.received > leftTruePeak + 5.f); // the right channel's

    two.GetInlet(0)->SetMessage("channel 1", YSE::T_GUI);
    peak.gotFloat = false;
    two.ResetDSP();
    two.Calculate(YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    CHECK_FALSE(peak.gotFloat); // 'channel' does not report
    two.GetInlet(0)->SetBang(YSE::T_GUI);
    two.ResetDSP();
    two.Calculate(YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(peak.gotFloat);
    CHECK(peak.received == doctest::Approx(20.f * std::log10(0.25f)).epsilon(1e-4));

    two.GetInlet(0)->SetInt(0, YSE::T_GUI); // 0: the highest of all
    two.ResetDSP();
    two.Calculate(YSE::T_DSP);
    two.FlushArmedOutlets(YSE::T_GUI);
    CHECK(peak.received == doctest::Approx(20.f * std::log10(0.5f)).epsilon(1e-4));
  }

  TEST_CASE("~loudness: 'interval' reports automatically (#1129)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    in = 0.f;
    rig.ld.GetInlet(0)->SetMessage("interval 100", YSE::T_GUI);
    int reports = 0;
    for (int k = 0; k < 375; ++k) // 1 s
      if (rig.Block(in)) reports++;
    CHECK(reports == 10);
    CHECK(rig.momentary.received == loudnessMeter::SILENCE);
    rig.ld.GetInlet(0)->SetMessage("interval 0", YSE::T_GUI);
    reports = 0;
    for (int k = 0; k < 375; ++k)
      if (rig.Block(in)) reports++;
    CHECK(reports == 0);
    // Malformed and unknown messages change nothing.
    rig.ld.SetMessage("interval", 0.f);
    rig.ld.SetMessage("interval x", 0.f);
    rig.ld.SetMessage("resets", 0.f);
    rig.ld.SetMessage("", 0.f);
    for (int k = 0; k < 375; ++k)
      if (rig.Block(in)) reports++;
    CHECK(reports == 0);
  }

  TEST_CASE("~loudness: 'reset' starts over at the next block, after a report that came with "
            "it (#1129)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    for (int k = 0; k < 750; ++k) {
      Sine(in, k, 0.1);
      rig.Block(in);
    }
    rig.ld.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.ld.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    in = 0.f;
    REQUIRE(rig.Block(in));
    CHECK(std::fabs(rig.integrated.received - -23.f) <= 0.1f); // the report comes first
    rig.ld.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.Block(in));
    CHECK(rig.integrated.received == loudnessMeter::SILENCE); // then a fresh start
    CHECK(rig.momentary.received == loudnessMeter::SILENCE);
    CHECK(rig.peak.received == loudnessMeter::SILENCE);
  }

  TEST_CASE("~loudness: the channel count and interval through a DumpJSON / ParseJSON round "
            "trip (#1129)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_LOUDNESS, "3 100");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_LOUDNESS, "");
      YSE::pHandle* c = src.CreateObject(YSE::OBJ::D_LOUDNESS, "999");
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      REQUIRE(c != nullptr);
      CHECK(a->GetInputs() == 3);
      CHECK(b->GetInputs() == 1);
      CHECK(c->GetInputs() == static_cast<int>(loudnessMeter::MAX_CHANNELS));
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~loudness");
    CHECK(a->GetParams() == "3 100");
    CHECK(a->GetInputs() == 3);
    CHECK(a->IsDSPInput(2));
    CHECK(a->GetOutputs() == 6);
    CHECK(b->GetParams() == "");
    CHECK(b->GetInputs() == 1);
  }

  TEST_CASE("~loudness: in a rendered patch, a stereo 1 kHz sine at -23 dBFS reads -23 LUFS, "
            "reported every 100 ms after a JSON round trip (#1129, #1237)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int mID = 0;
    unsigned int sID = 0;
    unsigned int iID = 0;
    unsigned int pID = 0;
    unsigned int rID = 0;
    unsigned int tID = 0;
    {
      patcherImplementation src(1, nullptr);
      const double amp = std::pow(10.0, -23.0 / 20.0);
      const std::string gain = std::to_string(amp);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "1000");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::D_MULTIPLY, gain);
      YSE::pHandle* ld = src.CreateObject(YSE::OBJ::D_LOUDNESS, "2 100");
      YSE::pHandle* mOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* sOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* iOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* pOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* rOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* tOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "ld1129");
      REQUIRE(osc != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(ld != nullptr);
      REQUIRE(control != nullptr);
      src.Connect(osc, 0, level, 0);
      src.Connect(level, 0, ld, 0);
      src.Connect(level, 0, ld, 1);
      src.Connect(control, 0, ld, 0);
      src.Connect(ld, 0, mOut, 0);
      src.Connect(ld, 1, sOut, 0);
      src.Connect(ld, 2, iOut, 0);
      src.Connect(ld, 3, rOut, 0);
      src.Connect(ld, 4, pOut, 0);
      src.Connect(ld, 5, tOut, 0);
      mID = mOut->GetID();
      sID = sOut->GetID();
      iID = iOut->GetID();
      pID = pOut->GetID();
      rID = rOut->GetID();
      tID = tOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* mOut = dst.GetHandleFromID(mID);
    YSE::pHandle* sOut = dst.GetHandleFromID(sID);
    YSE::pHandle* iOut = dst.GetHandleFromID(iID);
    YSE::pHandle* pOut = dst.GetHandleFromID(pID);
    YSE::pHandle* rOut = dst.GetHandleFromID(rID);
    YSE::pHandle* tOut = dst.GetHandleFromID(tID);
    REQUIRE(mOut != nullptr);
    REQUIRE(sOut != nullptr);
    REQUIRE(iOut != nullptr);
    REQUIRE(pOut != nullptr);
    REQUIRE(rOut != nullptr);
    REQUIRE(tOut != nullptr);

    for (int k = 0; k < 375 * 5; ++k) // 5 s
      dst.Calculate(YSE::T_DSP);
    CHECK(std::fabs(GuiFloat(mOut) - -23.f) <= 0.1f);
    CHECK(std::fabs(GuiFloat(sOut) - -23.f) <= 0.1f);
    CHECK(std::fabs(GuiFloat(iOut) - -23.f) <= 0.1f);
    CHECK(GuiFloat(pOut) == doctest::Approx(-23.f).epsilon(1e-2));
    // A steady tone has no loudness range. ~cycle starts on its crest, a
    // step from silence that the interpolator overshoots: the true peak
    // reads above the tone's crest until a reset.
    CHECK(GuiFloat(rOut) <= 0.05f);
    CHECK(GuiFloat(tOut) > -23.f);
    CHECK(GuiFloat(tOut) < -21.5f);

    // 'reset' through the receive, then silence on the integrated loudness
    // until 400 ms have been measured again.
    REQUIRE(dst.PassData(std::string("reset"), "ld1129", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    for (int k = 0; k < 75; ++k) // 200 ms: an interval report, no gated block yet
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(iOut) == loudnessMeter::SILENCE);
    REQUIRE(dst.PassBang("ld1129", YSE::T_GUI));
    for (int k = 0; k < 375; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(std::fabs(GuiFloat(iOut) - -23.f) <= 0.1f);
    // The reset kept the interpolator's memory, so the steady tone's true
    // peak is its crest.
    CHECK(std::fabs(GuiFloat(tOut) - -23.f) <= 0.1f);
  }

  TEST_CASE("~loudness: numbers, messages, a bang, blocks and the flush on the audio thread "
            "allocate nothing (#1129)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    Sine(in, 0, 0.5);
    const std::string reset = "reset";
    const std::string channel = "channel 1";
    const std::string interval = "interval 10";
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.ld.GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      rig.ld.GetInlet(0)->SetInt(0, YSE::T_DSP);
      rig.ld.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.ld.GetInlet(0)->SetMessage(channel, YSE::T_DSP);
      rig.ld.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      for (int k = 0; k < 400; ++k)
        rig.Block(in);
      rig.ld.GetInlet(0)->SetMessage(reset, YSE::T_DSP);
      rig.Block(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.peak.received > -10.f);
  }

} // TEST_SUITE("patcher")
