// Tests for the ~fzero patcher object (patcher/analysis/dFzero), issue
// #1130. The estimate is DSP::pitchTracker's and is tested in
// Tests/dsp/test_pitch_tracker.cpp; these show that the signal, the
// messages and the parameters reach it, that the reports go out right to
// left only through the flush after the block (#1037), that the settings
// survive a JSON round trip, and that a rendered ~cycle -> ~* -> ~fzero patch
// reads the pitch, the amplitude and the onset of a legato step, and stops
// reporting a pitch when a message narrows the range.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dFzero.h"
#include "dsp/analysis/pitchTracker.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::pitchTracker;
using YSE::PATCHER::dFzero;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double Pi = 3.14159265358979323846;

  // ~fzero with sinks on its three outlets. Block() runs one block and the
  // flush the patcher runs after its traversal.
  struct Rig {
    FloatSink pitch;
    FloatSink amplitude;
    MultiSink onset;
    dFzero fz;

    Rig() {
      Wire(fz, 0, pitch);
      Wire(fz, 1, amplitude);
      Wire(fz, 2, onset);
    }
    void Block(buffer& in) {
      pitch.gotFloat = false;
      amplitude.gotFloat = false;
      onset.reset();
      fz.ResetDSP();
      fz.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // runs CALC()
      // Nothing is sent from CALC() itself.
      CHECK_FALSE(pitch.gotFloat);
      CHECK_FALSE(amplitude.gotFloat);
      CHECK_FALSE(onset.gotBang);
      CHECK_FALSE(onset.gotList);
      fz.FlushArmedOutlets(YSE::T_GUI);
    }
  };

  // Block k of a sine at `hz` and `amp`, at 48 kHz.
  void Sine(buffer& b, std::uint64_t k, double hz, double amp = 0.5) {
    for (unsigned int i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(
          amp * std::sin(2.0 * Pi * hz * static_cast<double>(k * N + i) / 48000.0));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  double Relative(double value, double expected) {
    return std::fabs(value - expected) / expected;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~fzero: registry and shape (#1130)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_FZERO);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~fzero");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::ANY);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FZERO));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 10);
  }

  TEST_CASE("~fzero: reports what DSP::pitchTracker finds, right to left, after the block "
            "(#1130, #1037)") {
    ScopedSampleRate rate(48000);
    std::vector<char> log;
    OrderSink pitch;
    OrderSink amplitude;
    OrderSink onset;
    pitch.tag = 'p';
    amplitude.tag = 'a';
    onset.tag = 'o';
    pitch.log = &log;
    amplitude.log = &log;
    onset.log = &log;
    dFzero fz;
    Wire(fz, 0, pitch);
    Wire(fz, 1, amplitude);
    Wire(fz, 2, onset);
    pitchTracker ref;
    buffer in(N);

    std::uint64_t k = 0;
    int onsets = 0;
    for (; k < 100; ++k) {
      Sine(in, k, 220.0);
      fz.ResetDSP();
      fz.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      CHECK(log.empty());
      ref(in);
      fz.FlushArmedOutlets(YSE::T_GUI);
      if (ref.onsets() > 0) onsets++;
      CHECK(onset.count == onsets);
      if (ref.pitches() > 0) CHECK(pitch.lastFloat == ref.frequency());
      if (ref.reports() > 0) CHECK(amplitude.lastFloat == ref.amplitude());
      log.clear();
    }
    CHECK(onsets == 1);
    CHECK(onset.lastKind == OrderSink::BANG);
    CHECK(Relative(pitch.lastFloat, 220.0) < 1e-3);
    CHECK(amplitude.lastFloat > 0.499f);

    // A fifth up, legato: the block with the pitch onset sends all three,
    // the onset first and the pitch last.
    bool seen = false;
    for (; k < 200; ++k) {
      Sine(in, k, 330.0);
      fz.ResetDSP();
      fz.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      ref(in);
      fz.FlushArmedOutlets(YSE::T_GUI);
      if (ref.onsets() > 0) {
        seen = true;
        CHECK(log == std::vector<char>{'o', 'a', 'p'});
      }
      log.clear();
    }
    CHECK(seen);
    CHECK(onset.count == 2);
    CHECK(Relative(pitch.lastFloat, 330.0) < 1e-3);
  }

  TEST_CASE("~fzero: messages set the attributes; 'quiet' and malformed messages change "
            "nothing (#1130)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    pitchTracker ref;
    buffer in(N);
    std::uint64_t k = 0;
    const auto both = [&](double hz) {
      Sine(in, k++, hz);
      rig.Block(in);
      ref(in);
    };

    rig.fz.GetInlet(0)->SetMessage("size 1024", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("period 128", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("tolerance 0.1", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("onsetpitch 3", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("onsetperiod 4096", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("quiet 1", YSE::T_GUI);
    rig.fz.SetMessage("size", 0.f);
    rig.fz.SetMessage("size x", 0.f);
    rig.fz.SetMessage("sizes 4096", 0.f);
    rig.fz.SetMessage("", 0.f);
    ref.size(1024).period(128).tolerance(0.1f).onsetPitch(3.f).onsetPeriod(4096);
    for (int b = 0; b < 40; ++b) {
      both(440.0);
      CHECK(rig.pitch.gotFloat == (ref.pitches() > 0));
      if (rig.pitch.gotFloat) CHECK(rig.pitch.received == ref.frequency());
    }
    CHECK(Relative(rig.pitch.received, 440.0) < 1e-3);

    // freqmax below the tone: no pitch, the amplitude still comes.
    rig.fz.GetInlet(0)->SetMessage("freqmax 300", YSE::T_GUI);
    for (int b = 0; b < 10; ++b) {
      Sine(in, k++, 440.0);
      rig.Block(in);
      CHECK_FALSE(rig.pitch.gotFloat);
      CHECK(rig.amplitude.gotFloat);
    }
    // freqmin above it, freqmax back up: still none.
    rig.fz.GetInlet(0)->SetMessage("freqmax 4000", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("freqmin 500", YSE::T_GUI);
    for (int b = 0; b < 10; ++b) {
      Sine(in, k++, 440.0);
      rig.Block(in);
      CHECK_FALSE(rig.pitch.gotFloat);
    }
    // The threshold above the tone: nothing at all.
    rig.fz.GetInlet(0)->SetMessage("freqmin 40", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("threshold 0.6", YSE::T_GUI);
    for (int b = 0; b < 10; ++b) {
      Sine(in, k++, 440.0);
      rig.Block(in);
      CHECK_FALSE(rig.pitch.gotFloat);
      CHECK_FALSE(rig.amplitude.gotFloat);
    }
  }

  TEST_CASE("~fzero: 'onsetlist 1' sends the pitch and amplitude at the onset; 'onsetamp' sets "
            "the level (#1130)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    std::uint64_t k = 0;
    rig.fz.GetInlet(0)->SetMessage("onsetlist 1", YSE::T_GUI);
    rig.fz.GetInlet(0)->SetMessage("onsetamp 0.3", YSE::T_GUI);
    int lists = 0;
    for (int b = 0; b < 50; ++b) { // below onsetamp: no onset
      Sine(in, k++, 220.0, 0.2);
      rig.Block(in);
      if (rig.onset.gotList) lists++;
    }
    CHECK(lists == 0);
    for (int b = 0; b < 50; ++b) { // up to 0.5: one onset
      Sine(in, k++, 220.0, 0.5);
      rig.Block(in);
      CHECK_FALSE(rig.onset.gotBang);
      if (rig.onset.gotList) lists++;
    }
    REQUIRE(lists == 1);
    // A legato step: a list of the new pitch and the amplitude.
    std::string last;
    for (int b = 0; b < 50; ++b) {
      Sine(in, k++, 330.0, 0.5);
      rig.Block(in);
      if (rig.onset.gotList) {
        lists++;
        last = rig.onset.listValue;
      }
    }
    REQUIRE(lists == 2);
    const std::size_t space = last.find(' ');
    REQUIRE(space != std::string::npos);
    CHECK(Relative(std::stof(last.substr(0, space)), 330.0) < 1e-2);
    CHECK(std::stof(last.substr(space + 1)) == doctest::Approx(0.5f).epsilon(1e-3));

    // Back to bangs.
    rig.fz.GetInlet(0)->SetMessage("onsetlist 0", YSE::T_GUI);
    int bangs = 0;
    for (int b = 0; b < 50; ++b) {
      Sine(in, k++, 220.0, 0.5);
      rig.Block(in);
      CHECK_FALSE(rig.onset.gotList);
      if (rig.onset.gotBang) bangs++;
    }
    CHECK(bangs == 1);
  }

  TEST_CASE("~fzero: the settings through a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1130)") {
    ScopedSampleRate rate(48000);
    const std::string args = "1024 256 100 2000 0.05 0.2 2 4096 1 0.1";
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_FZERO, args);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_FZERO, "");
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      a->SetListData(0, "size 128"); // a message: not saved
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~fzero");
    CHECK(a->GetParams() == args);
    CHECK(b->GetParams() == "");

    // The parameters reach the class: the same settings on a fresh object
    // and on a pitchTracker give the same reports.
    dFzero fz;
    fz.SetParams(args);
    FloatSink pitch;
    FloatSink amplitude;
    MultiSink onset;
    Wire(fz, 0, pitch);
    Wire(fz, 1, amplitude);
    Wire(fz, 2, onset);
    pitchTracker ref;
    ref.size(1024)
        .period(256)
        .minFrequency(100.f)
        .maxFrequency(2000.f)
        .threshold(0.05f)
        .onsetAmplitude(0.2f)
        .onsetPitch(2.f)
        .onsetPeriod(4096)
        .tolerance(0.1f);
    buffer in(N);
    bool sawList = false;
    for (std::uint64_t k = 0; k < 60; ++k) {
      Sine(in, k, k < 30 ? 220.0 : 260.0); // 3 semitones up
      pitch.gotFloat = false;
      fz.ResetDSP();
      fz.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      fz.FlushArmedOutlets(YSE::T_GUI);
      ref(in);
      CHECK(pitch.gotFloat == (ref.pitches() > 0));
      if (pitch.gotFloat) CHECK(pitch.received == ref.frequency());
      if (onset.gotList) sawList = true; // onsetlist 1
      CHECK_FALSE(onset.gotBang);
    }
    CHECK(sawList);
  }

  TEST_CASE("~fzero: in a rendered patch, a ~cycle's pitch, amplitude and legato onset, and "
            "'freqmax' through a receive (#1130)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int pID = 0;
    unsigned int aID = 0;
    unsigned int oID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "220");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0");
      YSE::pHandle* fz = src.CreateObject(YSE::OBJ::D_FZERO, "2048 512 40 4000 0.01 0.1 1 2048 1");
      YSE::pHandle* split = src.CreateObject(YSE::OBJ::G_UNPACK, "0.0 0.0");
      YSE::pHandle* pOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* aOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* oOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* freq = src.CreateObject(YSE::OBJ::G_RECEIVE, "fz1130freq");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::G_RECEIVE, "fz1130gain");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "fz1130");
      REQUIRE(osc != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(fz != nullptr);
      REQUIRE(split != nullptr);
      REQUIRE(freq != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(control != nullptr);
      src.Connect(freq, 0, osc, 0);
      src.Connect(gain, 0, level, 1);
      src.Connect(osc, 0, level, 0);
      src.Connect(level, 0, fz, 0);
      src.Connect(control, 0, fz, 0);
      src.Connect(fz, 0, pOut, 0);
      src.Connect(fz, 1, aOut, 0);
      src.Connect(fz, 2, split, 0);
      src.Connect(split, 0, oOut, 0);
      pID = pOut->GetID();
      aID = aOut->GetID();
      oID = oOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* pOut = dst.GetHandleFromID(pID);
    YSE::pHandle* aOut = dst.GetHandleFromID(aID);
    YSE::pHandle* oOut = dst.GetHandleFromID(oID);
    REQUIRE(pOut != nullptr);
    REQUIRE(aOut != nullptr);
    REQUIRE(oOut != nullptr);

    for (int k = 0; k < 20; ++k) // silence: nothing reported
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(pOut) == 0.f);
    CHECK(GuiFloat(aOut) == 0.f);

    REQUIRE(dst.PassData(0.5f, "fz1130gain", YSE::T_GUI));
    for (int k = 0; k < 100; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(Relative(GuiFloat(pOut), 220.0) < 2e-3);
    CHECK(GuiFloat(aOut) == doctest::Approx(0.5f).epsilon(1e-2));

    // A fifth up: the onset's list carries the new pitch.
    REQUIRE(dst.PassData(330.f, "fz1130freq", YSE::T_GUI));
    for (int k = 0; k < 100; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(Relative(GuiFloat(pOut), 330.0) < 2e-3);
    CHECK(Relative(GuiFloat(oOut), 330.0) < 1e-2);

    // 'freqmax 300' through the receive: the pitch is no longer reported,
    // the amplitude still is.
    REQUIRE(dst.PassData(std::string("freqmax 300"), "fz1130", YSE::T_GUI));
    REQUIRE(dst.PassData(440.f, "fz1130freq", YSE::T_GUI));
    REQUIRE(dst.PassData(0.25f, "fz1130gain", YSE::T_GUI));
    for (int k = 0; k < 100; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(Relative(GuiFloat(pOut), 330.0) < 2e-3);
    CHECK(GuiFloat(aOut) == doctest::Approx(0.25f).epsilon(1e-2));
  }

  TEST_CASE("~fzero: messages, blocks and the flush on the audio thread allocate nothing "
            "(#1130)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    const std::string list = "onsetlist 1";
    const std::string size = "size 4096";
    const std::string period = "period 16";
    const std::string pitch = "onsetpitch 0.5";
    std::uint64_t k = 0;
    Sine(in, k++, 220.0);
    rig.Block(in);
    rig.onset.listValue.reserve(128); // the sink's own copy, not the object's
    int lists = 0;
    {
      TestHelpers::ProbeScope probe;
      rig.fz.GetInlet(0)->SetMessage(list, YSE::T_DSP);
      rig.fz.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      rig.fz.GetInlet(0)->SetMessage(period, YSE::T_DSP);
      rig.fz.GetInlet(0)->SetMessage(pitch, YSE::T_DSP);
      for (int b = 0; b < 100; ++b) {
        Sine(in, k++, b < 50 ? 220.0 : 330.0);
        rig.Block(in);
        if (rig.onset.gotList) lists++;
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(lists >= 1); // the list path ran inside the probe
  }

} // TEST_SUITE("patcher")
