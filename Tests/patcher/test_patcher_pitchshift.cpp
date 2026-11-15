// Tests for the ~pitchshift patcher object (patcher/delay/dPitchshift), issue
// #1153. The shifting is DSP::pitchShifter's and is tested in
// Tests/dsp/test_pitch_shifter.cpp; these show that the signals, numbers,
// attribute messages and settings reach one class per channel, that the
// latency outlet reports when it should, and, in rendered patches after a
// JSON round trip, that a ~cycle comes out shifted to the right pitch (by
// ~fzero and by its spectrum) with the latency reported to a number box.

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
#include "patcher/delay/dPitchshift.h"
#include "dsp/delay/pitchShifter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::IntSink;
using YSE::DSP::buffer;
using YSE::DSP::pitchShifter;
using YSE::PATCHER::dPitchshift;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Sinks for up to two channels and the latency.
  struct Rig {
    std::array<BufferSink, 2> out;
    IntSink latency;
    dPitchshift obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      const int channels = obj.ChannelCount();
      for (int c = 0; c < channels && c < 2; ++c)
        TestHelpers::Wire(obj, c, out[static_cast<std::size_t>(c)]);
      TestHelpers::Wire(obj, channels, latency);
    }
  };

  void Fill(buffer& b, unsigned int start, float scale = 1.f) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = scale * (std::sin(n * 0.05f) + 0.5f * std::sin(n * 1.3f));
    }
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
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~pitchshift: registry and shape, one channel and three (#1153)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PITCHSHIFT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~pitchshift");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::INT);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_PITCHSHIFT, "3 quality good");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 4);
    CHECK(three->GetOutputs() == 4);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(three->IsDSPInput(i));
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(three->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);
    CHECK(three->OutputDataType(3) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PITCHSHIFT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DELAY);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~pitchshift: numbers, attribute messages and a shift signal against the DSP "
            "class (#1153, #1036)") {
    Rig rig;
    pitchShifter reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* shift = nullptr) {
      rig.obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      rig.obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (shift != nullptr) rig.obj.GetInlet(1)->SetBuffer(shift, YSE::T_GUI);
      rig.obj.Calculate(YSE::T_DSP);
      buffer& expected = shift != nullptr ? reference(refIn, *shift) : reference(refIn);
      REQUIRE(rig.out[0].received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*rig.out[0].received, expected, 0.f));
    };

    for (int k = 0; k < 3; ++k)
      step();

    // A number in the shift inlet is a ratio, float or int.
    rig.obj.GetInlet(1)->SetFloat(1.5f, YSE::T_GUI);
    reference.ratio(1.5f);
    step();
    step();
    rig.obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    reference.ratio(2.f);
    step();

    // usecents 1: numbers are cents from now on.
    rig.obj.SetMessage("usecents 1", 0.f);
    reference.usesCents(true);
    rig.obj.GetInlet(1)->SetInt(-700, YSE::T_GUI);
    reference.cents(-700.f);
    step();
    rig.obj.GetInlet(1)->SetFloat(350.5f, YSE::T_GUI);
    reference.cents(350.5f);
    step();

    // pitchshift / pitchshiftcent, on either inlet, as messages and text.
    rig.obj.GetInlet(0)->SetMessage("pitchshift 0.8", YSE::T_GUI);
    reference.ratio(0.8f);
    step();
    rig.obj.GetInlet(1)->SetList("pitchshiftcent -300", YSE::T_GUI);
    reference.cents(-300.f);
    step();

    // quality by name and number, constantlatency, enabled.
    rig.obj.SetMessage("quality good", 0.f);
    reference.quality(pitchShifter::GOOD);
    step();
    step();
    rig.obj.SetMessage("constantlatency 1", 0.f);
    reference.constantLatency(true);
    step();
    rig.obj.SetMessage("quality 2", 0.f);
    reference.quality(pitchShifter::BETTER);
    for (int k = 0; k < 6; ++k)
      step();
    rig.obj.SetMessage("enabled 0", 0.f);
    reference.enabled(false);
    step();
    rig.obj.SetMessage("enabled 1", 0.f);
    reference.enabled(true);
    rig.obj.SetMessage("quality basic", 0.f);
    reference.quality(pitchShifter::BASIC);
    step();
    step();

    // Unknown words and bad values: ignored.
    rig.obj.SetMessage("quality worst", 0.f);
    rig.obj.SetMessage("quality 9", 0.f);
    rig.obj.SetMessage("pitchshift", 0.f);
    rig.obj.SetMessage("pitchshift abc", 0.f);
    rig.obj.SetMessage("pitch 2", 0.f);
    rig.obj.SetMessage("", 0.f);
    step();

    // A connected shift signal wins, in cents while usecents is on, read per
    // sample.
    buffer s(N);
    for (unsigned int i = 0; i < N; ++i)
      s.getPtr()[i] = i < N / 2 ? 200.f : 500.f;
    step(&s);
    step(&s);
    CHECK(rig.obj.Shifter(0).cents() == doctest::Approx(500.0).epsilon(1e-4));
    rig.obj.SetMessage("usecents 0", 0.f);
    reference.usesCents(false);
    s = 1.25f;
    step(&s);
    // Back to unconnected: the last number again.
    reference.cents(-300.f);
    step();

    // An unconnected channel inlet reads its number.
    Rig idle;
    idle.obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    for (int k = 0; k < 2; ++k) {
      idle.obj.ResetDSP();
      idle.obj.Calculate(YSE::T_DSP);
    }
    REQUIRE(idle.out[0].received != nullptr);
    // Ratio 1: the constant, two samples late.
    CHECK(idle.out[0].received->getPtr()[N - 1] == 0.25f);
  }

  TEST_CASE("~pitchshift: the latency outlet reports on reportlatency, on a change while it is "
            "on, and on getlatency (#1153, #1037)") {
    Rig rig;
    buffer in(N);
    in = 0.f;
    const auto block = [&]() {
      rig.latency.gotInt = false;
      rig.obj.ResetDSP();
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
    CHECK(rig.latency.received == 2);
    CHECK_FALSE(block()); // unchanged: nothing
    rig.obj.GetInlet(1)->SetFloat(2.f, YSE::T_GUI);
    CHECK(block());
    CHECK(rig.latency.received == 642);
    rig.obj.SetMessage("constantlatency 1", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 1666);
    rig.obj.SetMessage("quality best", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 4096);
    rig.obj.SetMessage("enabled 0", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 0);
    CHECK_FALSE(block());

    // getlatency 0 turns reporting off; a bare getlatency reports once.
    rig.obj.SetMessage("getlatency 0", 0.f);
    rig.obj.SetMessage("enabled 1", 0.f);
    CHECK_FALSE(block());
    rig.obj.SetMessage("getlatency", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 4096);
    CHECK_FALSE(block());
    rig.obj.SetMessage("quality good", 0.f);
    CHECK_FALSE(block());
    rig.obj.SetMessage("getlatency 1", 0.f);
    CHECK(block());
    CHECK(rig.latency.received == 3330);

    // From the settings: the first block reports.
    Rig args("1 reportlatency 1 pitchshift 0.5");
    args.obj.Calculate(YSE::T_DSP);
    args.obj.FlushArmedOutlets(YSE::T_GUI);
    CHECK(args.latency.gotInt);
    CHECK(args.latency.received == 386);
  }

  TEST_CASE("~pitchshift: the settings set the channels and attributes, and survive "
            "GetParams (#1153)") {
    const auto check = [](const std::string& args, int channels, float ratio,
                          pitchShifter::QUALITY quality, bool constant, bool enabled, bool cents) {
      CAPTURE(args);
      Rig rig(args);
      CHECK(rig.obj.GetParams() == args);
      CHECK(rig.obj.ChannelCount() == channels);
      rig.obj.Calculate(YSE::T_DSP);
      for (int c = 0; c < channels; ++c) {
        const pitchShifter& p = rig.obj.Shifter(static_cast<UInt>(c));
        CHECK(p.ratio() == doctest::Approx(ratio).epsilon(1e-6));
        CHECK(p.quality() == quality);
        CHECK(p.constantLatency() == constant);
        CHECK(p.enabled() == enabled);
        CHECK(p.usesCents() == cents);
      }
    };
    check("", 1, 1.f, pitchShifter::BASIC, false, true, false);
    check("2", 2, 1.f, pitchShifter::BASIC, false, true, false);
    check("2 pitchshift 1.5 quality better constantlatency 1 usecents 1", 2, 1.5f,
          pitchShifter::BETTER, true, true, true);
    check("1 pitchshiftcent 1200 enabled 0 quality 3", 1, 2.f, pitchShifter::BEST, false, false,
          false);
    check("0", 1, 1.f, pitchShifter::BASIC, false, true, false);
    check("40", 16, 1.f, pitchShifter::BASIC, false, true, false);
    check("1 quality nonsense pitchshift x", 1, 1.f, pitchShifter::BASIC, false, true, false);

    // A new setting rebuilds the object; an empty one is the default.
    Rig rig("3 pitchshift 2");
    CHECK(rig.obj.NumInputs() == 4);
    rig.obj.SetParams("");
    CHECK(rig.obj.ChannelCount() == 1);
    CHECK(rig.obj.NumInputs() == 2);
    CHECK(rig.obj.Shifter(0).ratio() == 1.f);
  }

  TEST_CASE("~pitchshift: two channels are shifted alike, each by its own class (#1153)") {
    Rig rig("2 pitchshift 1.3 quality good");
    pitchShifter left(pitchShifter::GOOD);
    pitchShifter right(pitchShifter::GOOD);
    left.ratio(1.3f);
    right.ratio(1.3f);
    buffer a(N);
    buffer b(N);
    buffer ra(N);
    buffer rb(N);
    for (unsigned int block = 0; block < 12; ++block) {
      CAPTURE(block);
      Fill(a, block * N);
      Fill(b, block * N + 99, -0.5f);
      Fill(ra, block * N);
      Fill(rb, block * N + 99, -0.5f);
      rig.obj.ResetDSP();
      rig.obj.GetInlet(0)->SetBuffer(&a, YSE::T_GUI);
      rig.obj.GetInlet(1)->SetBuffer(&b, YSE::T_GUI);
      rig.obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(*rig.out[0].received, left(ra), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*rig.out[1].received, right(rb), 0.f));
    }
  }

  TEST_CASE("~pitchshift: in a rendered patch, a ~cycle shifted a fifth up (by ~fzero and by "
            "its spectrum), then by a receive on longer grains and in the phase vocoder, with "
            "the latency in a number box, after a JSON round trip (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int pitchID = 0;
    unsigned int latencyID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "220");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      YSE::pHandle* shifter =
          src.CreateObject(YSE::OBJ::D_PITCHSHIFT, "1 pitchshiftcent 700 reportlatency 1");
      YSE::pHandle* fz = src.CreateObject(YSE::OBJ::D_FZERO, "");
      YSE::pHandle* pitch = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* latency = src.CreateObject(YSE::OBJ::G_INT, "");
      YSE::pHandle* amount = src.CreateObject(YSE::OBJ::G_RECEIVE, "ps1153shift");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "ps1153cmd");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(shifter != nullptr);
      REQUIRE(fz != nullptr);
      REQUIRE(pitch != nullptr);
      REQUIRE(latency != nullptr);
      REQUIRE(amount != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, gain, 0);
      src.Connect(gain, 0, shifter, 0);
      src.Connect(amount, 0, shifter, 1);
      src.Connect(cmd, 0, shifter, 0);
      src.Connect(shifter, 0, fz, 0);
      src.Connect(shifter, 0, dac, 0);
      src.Connect(fz, 0, pitch, 0);
      src.Connect(shifter, 1, latency, 0);
      pitchID = pitch->GetID();
      latencyID = latency->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~pitchshift") != std::string::npos);
    CHECK(dump.find("pitchshiftcent 700") != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* pitch = dst.GetHandleFromID(pitchID);
    YSE::pHandle* latency = dst.GetHandleFromID(latencyID);
    REQUIRE(pitch != nullptr);
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
    const auto check = [&](double target, int blocks) {
      CAPTURE(target);
      const std::vector<float> out = render(blocks);
      CHECK(std::abs(Loudest(out, target * 0.9, target * 1.1, 48000.0) - target) < 0.002 * target);
      CHECK(Amplitude(out, 220.0, 48000.0) < 0.05 * Amplitude(out, target, 48000.0));
      CHECK(std::abs(GuiFloat(pitch) - target) < 0.005 * target);
    };

    // A fifth up, at the granular latency for it.
    const double fifth = 220.0 * std::pow(2.0, 7.0 / 12.0);
    check(fifth, 128);
    CHECK(GuiFloat(latency) ==
          130.f + static_cast<float>(std::ceil((std::pow(2.0, 7.0 / 12.0) - 1.0) * 512.0)));

    // An octave down through the receive (a ratio), on the longer grains
    // that align a low pitch better, then the phase vocoder.
    REQUIRE(dst.PassData(std::string("quality good"), "ps1153cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(0.5f, "ps1153shift", YSE::T_GUI));
    check(110.0, 128);
    CHECK(GuiFloat(latency) == 770.f);
    REQUIRE(dst.PassData(std::string("quality better"), "ps1153cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(1.25f, "ps1153shift", YSE::T_GUI));
    check(275.0, 160);
    CHECK(GuiFloat(latency) == 2048.f);
  }

  TEST_CASE("~pitchshift: in a rendered patch, a ~sig into the shift inlet doubles a ~cycle "
            "with usecents off, and cents with it on (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto run = [](const std::string& args, const std::string& sig) {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* osc = p.CreateObject(YSE::OBJ::D_CYCLE, "300");
      YSE::pHandle* shift = p.CreateObject(YSE::OBJ::D_SIG, sig);
      YSE::pHandle* shifter = p.CreateObject(YSE::OBJ::D_PITCHSHIFT, args);
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(shift != nullptr);
      REQUIRE(shifter != nullptr);
      REQUIRE(dac != nullptr);
      p.Connect(osc, 0, shifter, 0);
      p.Connect(shift, 0, shifter, 1);
      p.Connect(shifter, 0, dac, 0);
      std::vector<float> out;
      for (int k = 0; k < 128; ++k) {
        p.Calculate(YSE::T_DSP);
        if (k >= 64) out.insert(out.end(), p.output[0].getPtr(), p.output[0].getPtr() + N);
      }
      return out;
    };
    for (const auto& c : {std::make_pair(std::string("1"), std::string("2")),
                          std::make_pair(std::string("1 usecents 1"), std::string("1200")),
                          std::make_pair(std::string("1 quality best"), std::string("2"))}) {
      CAPTURE(c.first);
      const std::vector<float> out = run(c.first, c.second);
      CHECK(std::abs(Loudest(out, 550.0, 650.0, 48000.0) - 600.0) < 1.0);
      CHECK(Amplitude(out, 300.0, 48000.0) < 0.05 * Amplitude(out, 600.0, 48000.0));
    }
  }

  TEST_CASE("~pitchshift: numbers, messages, blocks and the flush on the audio thread allocate "
            "nothing (#1153)") {
    Rig rig("1 reportlatency 1");
    buffer in(N);
    buffer s(N);
    in = 0.5f;
    s = 1.5f;
    const std::string quality = "quality better";
    const std::string basic = "quality basic";
    const std::string cents = "pitchshiftcent 300";
    const std::string get = "getlatency";
    rig.obj.Calculate(YSE::T_DSP);
    rig.obj.FlushArmedOutlets(YSE::T_GUI);
    {
      TestHelpers::ProbeScope probe;
      rig.obj.GetInlet(1)->SetFloat(1.25f, YSE::T_DSP);
      rig.obj.GetInlet(1)->SetInt(2, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetFloat(0.1f, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(quality, YSE::T_DSP);
      rig.obj.GetInlet(1)->SetList(cents, YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(get, YSE::T_DSP);
      rig.obj.Calculate(YSE::T_DSP);
      rig.obj.FlushArmedOutlets(YSE::T_DSP);
      rig.obj.GetInlet(0)->SetMessage(basic, YSE::T_DSP);
      rig.obj.ResetDSP();
      rig.obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      rig.obj.GetInlet(1)->SetBuffer(&s, YSE::T_DSP);
      rig.obj.Calculate(YSE::T_DSP);
      rig.obj.FlushArmedOutlets(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
