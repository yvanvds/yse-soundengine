// Tests for the ~retune patcher object (patcher/delay/dRetune), issue #1154.
// The correcting is DSP::retune's and is tested in Tests/dsp/test_retune.cpp;
// these show that the signal, the lock and detune numbers, the attribute
// messages, MIDI note lists and the settings reach the class, that the pitch
// and latency outlets report as they should, and, in rendered patches after
// a JSON round trip, that a detuned ~cycle comes out on the nearest enabled
// note (by ~fzero and by its spectrum) as the notes change through
// receives.

#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/delay/dRetune.h"
#include "dsp/delay/retune.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::IntSink;
using YSE::DSP::buffer;
using YSE::DSP::pitchShifter;
using YSE::DSP::retune;
using YSE::DSP::tuning;
using YSE::PATCHER::dRetune;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct Rig {
    BufferSink out;
    BufferSink pitch;
    IntSink latency;
    dRetune obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      TestHelpers::Wire(obj, 0, out);
      TestHelpers::Wire(obj, 1, pitch);
      TestHelpers::Wire(obj, 2, latency);
    }
  };

  // A 450 Hz sine at 48 kHz, from sample `start`.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>(
          0.5 * std::sin(2.0 * PI * 450.0 * static_cast<double>(start + i) / 48000.0));
  }

  // The amplitude of `v` at `hz`, Hann-windowed.
  double Amplitude(const std::vector<float>& v, double hz, double rate) {
    double re = 0.0;
    double im = 0.0;
    double sum = 0.0;
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < v.size(); ++n) {
      const double h =
          0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / static_cast<double>(v.size()));
      re += h * v[n] * std::cos(w * static_cast<double>(n));
      im -= h * v[n] * std::sin(w * static_cast<double>(n));
      sum += h;
    }
    return 2.0 * std::hypot(re, im) / sum;
  }

  // The loudest frequency of `v` between `low` and `high` Hz, to 0.05 Hz.
  double Loudest(const std::vector<float>& v, double low, double high, double rate) {
    double best = low;
    double level = -1.0;
    const auto steps = static_cast<int>(high - low);
    for (int i = 0; i <= steps; ++i) {
      const double hz = low + i;
      const double a = Amplitude(v, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    const double centre = best;
    for (int i = 0; i <= 40; ++i) {
      const double hz = centre - 1.0 + 0.05 * i;
      const double a = Amplitude(v, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    return best;
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  constexpr double G_SHARP4 = 415.3046975799451;
  constexpr double A_SHARP4 = 466.1637615180899;
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~retune: registry and shape (#1154)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_RETUNE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~retune");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 3);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_RETUNE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DELAY);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~retune: numbers, attribute messages and note lists against the DSP class (#1154, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig;
    retune reference;
    buffer objIn(N);
    buffer refIn(N);
    buffer lock(N);
    buffer detune(N);
    float lockHz = 0.f;
    float detuneCents = 0.f;
    unsigned int start = 0;

    const auto step = [&]() {
      rig.obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      lock = lockHz;
      detune = detuneCents;
      rig.obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      rig.obj.Calculate(YSE::T_DSP);
      buffer& expected = reference(refIn, lock, detune);
      REQUIRE(rig.out.received != nullptr);
      REQUIRE(rig.pitch.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*rig.out.received, expected, 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*rig.pitch.received, reference.pitch(), 0.f));
    };
    const auto steps = [&](int count) {
      for (int k = 0; k < count; ++k)
        step();
    };

    steps(40);
    CHECK(rig.obj.Retuner().frequency() == doctest::Approx(450.0).epsilon(0.001));

    // The correction attributes, as messages on any inlet.
    rig.obj.SetMessage("correction_amount 50", 0.f);
    reference.amount(50.f);
    steps(4);
    rig.obj.GetInlet(1)->SetMessage("correction_threshold 12.5", YSE::T_GUI);
    reference.threshold(12.5f);
    rig.obj.GetInlet(2)->SetList("correction_ambience_threshold 0.2", YSE::T_GUI);
    reference.ambience(0.2f);
    rig.obj.SetMessage("correction_bypass 30", 0.f);
    reference.bypass(30.f);
    rig.obj.SetMessage("windowsize 128", 0.f);
    reference.period(128);
    rig.obj.SetMessage("pitchdetection 1", 0.f);
    reference.pitchDetection(true);
    steps(4);
    rig.obj.SetMessage("correction_amount 100", 0.f);
    reference.amount(100.f);

    // The notes: a Scala list in cents and ratios, the base, a subset.
    rig.obj.SetMessage("notelist 9/8 400 500 3/2 900 1100", 0.f);
    const float major[] = {tuning::ratioToCents(9.f / 8.f), 400.f, 500.f,
                           tuning::ratioToCents(1.5f),      900.f, 1100.f};
    reference.notes().notes(major, 6);
    rig.obj.SetMessage("notebase 9", 0.f);
    reference.notes().base(9);
    steps(6);
    rig.obj.SetMessage("enablednotes 0 2 4", 0.f);
    reference.notes().enabled(0b10101);
    steps(3);
    rig.obj.SetMessage("enablednotes all", 0.f);
    reference.notes().enableAll();
    steps(3);

    // MIDI notes: a list into the left inlet, and the list word.
    rig.obj.GetInlet(0)->SetList("69 0 71 0", YSE::T_GUI);
    reference.notes().midiNote(69, 0).midiNote(71, 0);
    steps(3);
    rig.obj.SetMessage("list 71 100", 0.f);
    reference.notes().midiNote(71, 100);
    steps(3);
    rig.obj.SetMessage("flush", 0.f);
    reference.notes().enableAll();
    steps(3);
    rig.obj.SetMessage("defaulttuning", 0.f);
    reference.notes().chromatic();
    steps(3);

    // The lock and detune inlets take numbers.
    rig.obj.GetInlet(1)->SetFloat(500.f, YSE::T_GUI);
    lockHz = 500.f;
    steps(4);
    rig.obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    lockHz = 0.f;
    rig.obj.GetInlet(2)->SetInt(-30, YSE::T_GUI);
    detuneCents = -30.f;
    steps(4);

    // quality and retune.
    rig.obj.SetMessage("quality good", 0.f);
    reference.quality(pitchShifter::GOOD);
    steps(4);
    rig.obj.SetMessage("quality 2", 0.f);
    reference.quality(pitchShifter::BETTER);
    steps(20);
    rig.obj.SetMessage("retune 0", 0.f);
    reference.enabled(false);
    steps(2);
    rig.obj.SetMessage("retune 1", 0.f);
    reference.enabled(true);
    rig.obj.SetMessage("quality basic", 0.f);
    reference.quality(pitchShifter::BASIC);
    steps(4);

    // use_16bit is accepted and changes nothing; unknown words and bad
    // values are ignored.
    rig.obj.SetMessage("use_16bit 1", 0.f);
    rig.obj.SetMessage("quality worst", 0.f);
    rig.obj.SetMessage("correction_amount", 0.f);
    rig.obj.SetMessage("correction_amount abc", 0.f);
    rig.obj.SetMessage("notebase x", 0.f);
    rig.obj.SetMessage("retunes 1", 0.f);
    rig.obj.SetMessage("", 0.f);
    steps(3);
    CHECK(rig.obj.Dropped() == 0);

    // An unconnected signal inlet reads its number: amount 0 (no
    // correction), a constant two samples late.
    Rig idle("440 correction_amount 0");
    idle.obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    for (int k = 0; k < 2; ++k) {
      idle.obj.ResetDSP();
      idle.obj.Calculate(YSE::T_DSP);
    }
    REQUIRE(idle.out.received != nullptr);
    CHECK(idle.out.received->getPtr()[N - 1] == 0.25f);
  }

  TEST_CASE("~retune: the latency outlet reports on reportlatency, on a change while it is on, "
            "and on getlatency (#1154, #1037)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig;
    buffer in(N);
    unsigned int start = 0;
    const auto block = [&]() {
      rig.latency.gotInt = false;
      rig.obj.ResetDSP();
      Fill(in, start);
      start += N;
      rig.obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      rig.obj.Calculate(YSE::T_DSP);
      // Armed in the block, sent after it.
      CHECK_FALSE(rig.latency.gotInt);
      rig.obj.FlushArmedOutlets(YSE::T_GUI);
      return rig.latency.gotInt;
    };

    CHECK_FALSE(block()); // off by default
    rig.obj.SetMessage("reportlatency 1", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == static_cast<int>(rig.obj.Retuner().latency()));
    for (int k = 0; k < 40; ++k)
      block();
    // Corrected from 450 to 440 Hz.
    CHECK(rig.latency.received == static_cast<int>(rig.obj.Retuner().latency()));
    CHECK(rig.latency.received > 130);
    rig.obj.SetMessage("quality best", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 4096);
    CHECK_FALSE(block());
    rig.obj.SetMessage("retune 0", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 0);

    // getlatency 0 turns reporting off; a bare getlatency reports once.
    rig.obj.SetMessage("getlatency 0", 0.f);
    rig.obj.SetMessage("retune 1", 0.f);
    CHECK_FALSE(block());
    rig.obj.SetMessage("getlatency", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 4096);
    CHECK_FALSE(block());
    rig.obj.SetMessage("quality better", 0.f);
    CHECK_FALSE(block());
    rig.obj.SetMessage("getlatency 1", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 2048);

    // From the settings: the first block reports.
    Rig args("440 reportlatency 1 quality good retune 0");
    args.obj.Calculate(YSE::T_DSP);
    args.obj.FlushArmedOutlets(YSE::T_GUI);
    CHECK(args.latency.gotInt);
    CHECK(args.latency.received == 0);
  }

  TEST_CASE("~retune: the settings set the attributes and the notes, and survive GetParams "
            "(#1154)") {
    {
      Rig rig;
      CHECK(rig.obj.GetParams() == "");
      rig.obj.Calculate(YSE::T_DSP);
      const retune& r = rig.obj.Retuner();
      CHECK(r.amount() == 100.f);
      CHECK(r.threshold() == 0.f);
      CHECK(r.ambience() == doctest::Approx(0.3f));
      CHECK(r.bypass() == 100.f);
      CHECK(r.period() == retune::DEFAULT_PERIOD);
      CHECK(r.quality() == pitchShifter::BASIC);
      CHECK(r.enabled());
      CHECK_FALSE(r.pitchDetection());
      CHECK(r.notes().size() == 12);
      CHECK(r.notes().enabled() == ~U64(0));
      CHECK(r.notes().standardPitch() == 440.f);
    }
    {
      const std::string args = "442 correction_amount 75 correction_threshold 5 "
                               "correction_ambience_threshold 0.25 correction_bypass 40 notelist "
                               "200 400 500 700 900 1100 notebase 9 enablednotes 0 2 4 quality "
                               "better retune 1 pitchdetection 1 windowsize 64 use_16bit 1";
      Rig rig(args);
      CHECK(rig.obj.GetParams() == args);
      rig.obj.Calculate(YSE::T_DSP);
      const retune& r = rig.obj.Retuner();
      CHECK(r.amount() == 75.f);
      CHECK(r.threshold() == 5.f);
      CHECK(r.ambience() == doctest::Approx(0.25f));
      CHECK(r.bypass() == 40.f);
      CHECK(r.period() == 64);
      CHECK(r.quality() == pitchShifter::BETTER);
      CHECK(r.enabled());
      CHECK(r.pitchDetection());
      CHECK(r.notes().standardPitch() == 442.f);
      CHECK(r.notes().base() == 9);
      REQUIRE(r.notes().size() == 7);
      CHECK(r.notes().note(1) == 200.f);
      CHECK(r.notes().note(6) == 1100.f);
      CHECK(r.notes().enabled() == 0b10101);
    }
    {
      // No standard pitch: 440. Bad values are skipped; retune 0.
      Rig rig("retune 0 quality nonsense notebase -1 correction_amount x");
      rig.obj.Calculate(YSE::T_DSP);
      const retune& r = rig.obj.Retuner();
      CHECK(r.notes().standardPitch() == 440.f);
      CHECK_FALSE(r.enabled());
      CHECK(r.quality() == pitchShifter::BASIC);
      CHECK(r.notes().base() == 11);
      CHECK(r.amount() == 100.f);
    }
    {
      // A new setting replaces the old; an empty one is the default.
      Rig rig("415 notebase 2 correction_amount 10");
      rig.obj.SetParams("");
      rig.obj.Calculate(YSE::T_DSP);
      CHECK(rig.obj.Retuner().notes().standardPitch() == 440.f);
      CHECK(rig.obj.Retuner().notes().base() == 0);
      CHECK(rig.obj.Retuner().amount() == 100.f);
    }
  }

  TEST_CASE("~retune: in a rendered patch, a ~cycle at 450 Hz comes out on A (by ~fzero and by "
            "its spectrum) with its pitch and latency in number boxes, then on G# and back as "
            "notes are disabled, the scale changes and the amount and lock change through "
            "receives, after a JSON "
            "round trip (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int correctedID = 0;
    unsigned int detectedID = 0;
    unsigned int latencyID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "450");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      YSE::pHandle* tuner = src.CreateObject(
          YSE::OBJ::D_RETUNE,
          "440 notelist 200 400 500 700 900 1100 notebase 9 pitchdetection 1 reportlatency 1");
      YSE::pHandle* fz = src.CreateObject(YSE::OBJ::D_FZERO, "");
      YSE::pHandle* corrected = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1");
      YSE::pHandle* detected = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* latency = src.CreateObject(YSE::OBJ::G_INT, "");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "rt1154cmd");
      YSE::pHandle* lock = src.CreateObject(YSE::OBJ::G_RECEIVE, "rt1154lock");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      for (YSE::pHandle* h :
           {osc, gain, tuner, fz, corrected, snap, detected, latency, cmd, lock, dac})
        REQUIRE(h != nullptr);
      src.Connect(osc, 0, gain, 0);
      src.Connect(gain, 0, tuner, 0);
      src.Connect(cmd, 0, tuner, 0);
      src.Connect(lock, 0, tuner, 1);
      src.Connect(tuner, 0, fz, 0);
      src.Connect(tuner, 0, dac, 0);
      src.Connect(fz, 0, corrected, 0);
      src.Connect(tuner, 1, snap, 0);
      src.Connect(snap, 0, detected, 0);
      src.Connect(tuner, 2, latency, 0);
      correctedID = corrected->GetID();
      detectedID = detected->GetID();
      latencyID = latency->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~retune") != std::string::npos);
    CHECK(dump.find("notelist 200 400 500 700 900 1100 notebase 9") != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* corrected = dst.GetHandleFromID(correctedID);
    YSE::pHandle* detected = dst.GetHandleFromID(detectedID);
    YSE::pHandle* latency = dst.GetHandleFromID(latencyID);
    REQUIRE(corrected != nullptr);
    REQUIRE(detected != nullptr);
    REQUIRE(latency != nullptr);

    const auto render = [&](int blocks) {
      std::vector<float> out;
      for (int k = 0; k < blocks; ++k) {
        dst.Calculate(YSE::T_DSP);
        if (k >= blocks / 2)
          out.insert(out.end(), dst.output[0].getPtr(), dst.output[0].getPtr() + N);
      }
      return out;
    };
    const auto check = [&](double target) {
      CAPTURE(target);
      const std::vector<float> out = render(256);
      CHECK(std::abs(Loudest(out, 380.0, 520.0, 48000.0) - target) < 0.002 * target);
      CHECK(std::abs(GuiFloat(corrected) - target) < 0.005 * target);
      // What is left at 450 Hz, where it is far enough from the target to
      // tell apart: the basic grains' crossfades spread a little (see
      // Tests/dsp/test_retune.cpp).
      if (std::abs(target - 450.0) > 9.0)
        CHECK(Amplitude(out, 450.0, 48000.0) < 0.1 * Amplitude(out, target, 48000.0));
      // The pitch outlet: the input's.
      CHECK(GuiFloat(detected) == doctest::Approx(450.0).epsilon(0.002));
    };

    // A major on A: 450 Hz goes to A.
    check(440.0);
    const double toA = std::ceil((1.0 - 440.0 / 450.0) * 512.0) + 130.0;
    CHECK(std::abs(GuiFloat(latency) - toA) <= 1.f);

    // A disabled through the receive: the nearest enabled note is G#.
    REQUIRE(dst.PassData(std::string("enablednotes 1 2 3 4 5 6"), "rt1154cmd", YSE::T_GUI));
    check(G_SHARP4);
    // The chromatic scale, with A disabled by a MIDI note list: A#.
    REQUIRE(dst.PassData(std::string("defaulttuning"), "rt1154cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(std::string("notebase 0"), "rt1154cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(std::string("69 0"), "rt1154cmd", YSE::T_GUI));
    check(A_SHARP4);
    // Every note back, half the correction.
    REQUIRE(dst.PassData(std::string("flush"), "rt1154cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(std::string("correction_amount 50"), "rt1154cmd", YSE::T_GUI));
    check(440.0 * std::pow(450.0 / 440.0, 0.5));
    // Locked onto 500 Hz, at the whole amount; then no correction.
    REQUIRE(dst.PassData(std::string("correction_amount 100"), "rt1154cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(500.f, "rt1154lock", YSE::T_GUI));
    check(500.0);
    REQUIRE(dst.PassData(std::string("retune 0"), "rt1154cmd", YSE::T_GUI));
    check(450.0);
    CHECK(GuiFloat(latency) == 0.f);
  }

  TEST_CASE("~retune: numbers, messages, note lists, blocks and the flush on the audio thread "
            "allocate nothing (#1154)") {
    Rig rig("440 reportlatency 1 pitchdetection 1");
    buffer in(N);
    in = 0.5f;
    const std::string amount = "correction_amount 60";
    const std::string notes = "notelist 200 400 500 700 900 1100 9/8";
    const std::string base = "notebase 9";
    const std::string subset = "enablednotes 0 2 4";
    const std::string all = "enablednotes all";
    const std::string midi = "60 0 62 100";
    const std::string quality = "quality better";
    const std::string get = "getlatency";
    const std::string reset = "defaulttuning";
    const std::string flush = "flush";
    rig.obj.Calculate(YSE::T_DSP);
    rig.obj.FlushArmedOutlets(YSE::T_GUI);
    {
      TestHelpers::ProbeScope probe;
      rig.obj.GetInlet(1)->SetFloat(300.f, YSE::T_DSP);
      rig.obj.GetInlet(2)->SetInt(12, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetFloat(0.1f, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(amount, YSE::T_DSP);
      rig.obj.GetInlet(1)->SetMessage(notes, YSE::T_DSP);
      rig.obj.GetInlet(2)->SetMessage(base, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(subset, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetList(midi, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(quality, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(get, YSE::T_DSP);
      rig.obj.Calculate(YSE::T_DSP);
      rig.obj.FlushArmedOutlets(YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(all, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(reset, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(flush, YSE::T_DSP);
      rig.obj.ResetDSP();
      rig.obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      rig.obj.Calculate(YSE::T_DSP);
      rig.obj.FlushArmedOutlets(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.obj.Dropped() == 0);
  }

} // TEST_SUITE("patcher")
