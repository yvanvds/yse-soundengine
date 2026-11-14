// Tests for ~fbinshift (patcher/spectral/dFbinshift), issue #1165. The
// shifting is DSP::spectralFreqShift's and is tested in
// Tests/dsp/test_spectral_freq_shift.cpp; these show:
//  - the shape, and that outside a ~pfft the real and imaginary parts pass
//    through unchanged, unconnected inlets reading their numbers;
//  - inside a ~pfft ~fbinshift is frame-wide: it takes in every pass before
//    it puts out any, so a bin shifted down into an earlier pass lands
//    there; the shift from the argument, a number on inlet 2, a signal
//    there, and clip;
//  - in a rendered ~pfft a single-bin spectrum shifted up and down (and
//    down through 0 Hz) lands on the right bin, with the right angle frame
//    after frame, at FFT size 256 (one pass) and 2048 (eight passes, the
//    bin crossing pass boundaries), against DSP::stft in closed form;
//  - a rich signal through a rendered ~fbinshift after a JSON round trip,
//    with a shift on the inlet and clip, is DSP::stft with
//    DSP::spectralFreqShift;
//  - the arguments survive a JSON round trip and rendering allocates
//    nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/spectralFreqShift.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dFbinshift.h"
#include "patcher/spectral/spectralPass.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::spectralFreqShift;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::dFbinshift;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;
  constexpr UInt RATE = 48000;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {
      YSE::SAMPLERATE = RATE;
    }
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
    SampleRateGuard(const SampleRateGuard&) = delete;
    SampleRateGuard& operator=(const SampleRateGuard&) = delete;
    SampleRateGuard(SampleRateGuard&&) = delete;
    SampleRateGuard& operator=(SampleRateGuard&&) = delete;
  };

  double Wrap(double a) {
    return a - 2.0 * PI * std::floor(a / (2.0 * PI) + 0.5);
  }

  // `bins` bins of an FFT of `size` in Hz at RATE.
  float Hz(double bins, UInt size) {
    return static_cast<float>(bins * RATE / size);
  }

  // A rich signal: band content plus deterministic noise.
  float Rich(std::size_t n) {
    unsigned int s = static_cast<unsigned int>(n) * 2654435761u + 12345u;
    s ^= s >> 13;
    s *= 2246822519u;
    s ^= s >> 16;
    return 0.5f * static_cast<float>(std::sin(0.07 * static_cast<double>(n))) +
           0.3f * (static_cast<float>(s >> 8) / 16777216.f - 0.5f);
  }

  // `blocks` blocks of Rich through the patcher as an insert, per channel.
  std::vector<std::vector<float>> Render(patcherImplementation& p, unsigned int channels,
                                         unsigned int blocks) {
    std::vector<std::vector<float>> out(channels);
    MULTICHANNELBUFFER io(channels);
    for (auto& b : io)
      b.resize(N);
    std::size_t at = 0;
    for (unsigned int block = 0; block < blocks; ++block) {
      for (unsigned int c = 0; c < channels; ++c)
        for (unsigned int i = 0; i < N; ++i)
          io[c].getPtr()[i] = Rich(at + i);
      p.ProcessAsInsert(io);
      for (unsigned int c = 0; c < channels; ++c)
        out[c].insert(out[c].end(), io[c].getPtr(), io[c].getPtr() + N);
      at += N;
    }
    return out;
  }

  // `length` samples of Rich through DSP::stft in blocks, `f` called for
  // every frame: what a ~pfft with the same settings must put out.
  template <class F> std::vector<float> Reference(stft& fx, F& f, std::size_t length) {
    std::vector<float> out;
    buffer b(N);
    for (std::size_t start = 0; start < length; start += N) {
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = Rich(start + i);
      buffer& o = fx(b, f);
      out.insert(out.end(), o.getPtr(), o.getPtr() + N);
    }
    return out;
  }

  float WorstOf(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.f;
    for (std::size_t n = 0; n < a.size(); ++n)
      worst = std::max(worst, std::abs(a[n] - b[n]));
    return worst;
  }

  float PeakOf(const std::vector<float>& a) {
    float peak = 0.f;
    for (const float v : a)
      peak = std::max(peak, std::abs(v));
    return peak;
  }

  // A ~pfft pass of `count` bins at `firstBin` of a frame of `bins` bins.
  YSE::PATCHER::spectralPass Pass(UInt size, UInt bins, UInt pass, U64 frame, bool emitting) {
    YSE::PATCHER::spectralPass p;
    p.fftSize = size;
    p.bins = bins;
    p.hop = size / 4;
    p.overlap = 4;
    p.count = N;
    p.passes = bins / N;
    p.pass = pass;
    p.firstBin = pass * N;
    p.frame = frame;
    p.emitting = emitting;
    p.stages = 2;
    p.stage = emitting ? 1 : 0;
    return p;
  }

  // A standalone ~fbinshift with a sink on each outlet, driven pass by pass.
  struct rig {
    BufferSink re;
    BufferSink im;
    dFbinshift shifter;
    rig() {
      TestHelpers::Wire(shifter, 0, re);
      TestHelpers::Wire(shifter, 1, im);
    }

    // Every pass of a frame of `bins` bins consumed, real part `real`
    // (frame-long), imaginary unconnected; then every pass emitted, the
    // frame's output returned.
    std::vector<float> Frame(UInt size, UInt bins, U64 frame, const std::vector<float>& real,
                             std::vector<float>* imaginary = nullptr, buffer* shift = nullptr) {
      buffer in(N);
      for (UInt p = 0; p < bins / N; ++p) {
        std::copy(real.begin() + p * N, real.begin() + (p + 1) * N, in.getPtr());
        const YSE::PATCHER::spectralPass pass = Pass(size, bins, p, frame, false);
        const YSE::PATCHER::spectralPassScope scope(&pass);
        shifter.ResetDSP();
        shifter.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        if (shift != nullptr) shifter.GetInlet(2)->SetBuffer(shift, YSE::T_GUI);
        re.received = nullptr;
        shifter.Calculate(YSE::T_DSP);
        CHECK(re.received == nullptr);
      }
      std::vector<float> out;
      for (UInt p = 0; p < bins / N; ++p) {
        const YSE::PATCHER::spectralPass pass = Pass(size, bins, p, frame, true);
        const YSE::PATCHER::spectralPassScope scope(&pass);
        shifter.ResetDSP();
        shifter.Calculate(YSE::T_DSP);
        REQUIRE(re.received != nullptr);
        REQUIRE(im.received != nullptr);
        REQUIRE(re.received->getLength() == N);
        out.insert(out.end(), re.received->getPtr(), re.received->getPtr() + N);
        if (imaginary != nullptr)
          imaginary->insert(imaginary->end(), im.received->getPtr(), im.received->getPtr() + N);
      }
      return out;
    }
  };

  std::vector<float> Spike(UInt bins, UInt at, float value = 1.f) {
    std::vector<float> s(bins, 0.f);
    s[at] = value;
    return s;
  }

  // Every value of `s` but the one at `at` is 0.
  bool OnlyAt(const std::vector<float>& s, UInt at) {
    for (UInt k = 0; k < s.size(); ++k)
      if (k != at && s[k] != 0.f) return false;
    return true;
  }
} // namespace

TEST_SUITE("patcher") {
  TEST_CASE("~fbinshift: registry, category and shape (#1165)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_FBINSHIFT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~fbinshift");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 2);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FBINSHIFT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 2);
    CHECK(o->SpectralFrameWide());
  }

  TEST_CASE("~fbinshift: outside a ~pfft the real and imaginary parts pass through; "
            "unconnected inlets read their numbers (#1165)") {
    rig r;
    r.shifter.SetParams("500");
    buffer re(N);
    buffer im(N);
    for (unsigned int i = 0; i < N; ++i) {
      re.getPtr()[i] = std::sin(0.1f * static_cast<float>(i));
      im.getPtr()[i] = 0.25f * static_cast<float>(i);
    }
    r.shifter.ResetDSP();
    r.shifter.GetInlet(0)->SetBuffer(&re, YSE::T_GUI);
    r.shifter.GetInlet(1)->SetBuffer(&im, YSE::T_GUI);
    r.shifter.Calculate(YSE::T_DSP);
    REQUIRE(r.re.received != nullptr);
    REQUIRE(r.im.received != nullptr);
    CHECK(std::equal(re.getPtr(), re.getPtr() + N, r.re.received->getPtr()));
    CHECK(std::equal(im.getPtr(), im.getPtr() + N, r.im.received->getPtr()));
    CHECK(r.re.received->getLength() == N);

    rig u;
    u.shifter.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    u.shifter.GetInlet(1)->SetInt(-2, YSE::T_GUI);
    u.shifter.ResetDSP();
    u.shifter.Calculate(YSE::T_DSP);
    REQUIRE(u.re.received != nullptr);
    CHECK(u.re.received->getPtr()[0] == 0.5f);
    CHECK(u.re.received->getPtr()[N - 1] == 0.5f);
    CHECK(u.im.received->getPtr()[7] == -2.f);
  }

  TEST_CASE("~fbinshift: inside a ~pfft it takes in the whole frame before it puts out any, "
            "so a bin moves down into an earlier pass; the shift and clip (#1165)") {
    SampleRateGuard guard;
    // 512: 256 bins in two passes of 93.75 Hz. Bin 200 (pass 1) down 100
    // bins lands on 100 (pass 0), and frame 0 has angle 0.
    rig r;
    r.shifter.SetParams("-9375");
    std::vector<float> imaginary;
    const std::vector<float> down = r.Frame(512, 256, 0, Spike(256, 200), &imaginary);
    CHECK(OnlyAt(down, 100));
    CHECK(down[100] == 1.f);
    CHECK(OnlyAt(imaginary, 1000));

    // The same as the class on the whole frame.
    spectralFreqShift fn(-9375.f);
    std::vector<float> re = Spike(256, 200);
    std::vector<float> im(256, 0.f);
    const YSE::PATCHER::spectralPass whole = Pass(512, 256, 0, 0, false);
    stftFrame f = whole.Frame(re.data(), im.data());
    f.count = 256;
    fn(f);
    CHECK(re == down);

    // A number on inlet 2 overrides the argument from the next frame: 40 up
    // 80 bins = 120. 80 bins * hop is whole cycles, so the angle stays 0.
    r.shifter.GetInlet(2)->SetFloat(7500.f, YSE::T_GUI);
    CHECK(OnlyAt(r.Frame(512, 256, 1, Spike(256, 40)), 120));
    std::vector<float> up = r.Frame(512, 256, 2, Spike(256, 40));
    CHECK(OnlyAt(up, 120));
    CHECK(up[120] == doctest::Approx(1.f));

    // A signal on inlet 2, read at the frame's first bin: 3750 Hz (40 bins)
    // there, the rest of the pass ignored.
    rig s;
    buffer shift(N);
    shift = 90000.f;
    shift.getPtr()[0] = 3750.f;
    CHECK(OnlyAt(s.Frame(512, 256, 0, Spike(256, 160), nullptr, &shift), 200));

    // clip drops what folding would bring back: 20 - 30 = -10, folded to
    // 10 (the real part of a real spike stays).
    rig t;
    t.shifter.GetInlet(2)->SetFloat(-2812.5f, YSE::T_GUI);
    CHECK(OnlyAt(t.Frame(512, 256, 0, Spike(256, 20)), 10));
    t.shifter.GetInlet(0)->SetMessage("clip 1", YSE::T_GUI);
    CHECK(OnlyAt(t.Frame(512, 256, 4, Spike(256, 20)), 1000));
    t.shifter.GetInlet(0)->SetMessage("clip 0", YSE::T_GUI);
    t.shifter.GetInlet(0)->SetMessage("clip", YSE::T_GUI); // no number: ignored
    t.shifter.GetInlet(0)->SetMessage("freqshift 4", YSE::T_GUI); // not a message: ignored
    CHECK(OnlyAt(t.Frame(512, 256, 8, Spike(256, 20)), 10));

    // bins 128: the frame cut to its first pass.
    rig c;
    c.shifter.SetParams("5625 128");
    CHECK(c.shifter.GetParams() == "5625 128");
    CHECK(OnlyAt(c.Frame(512, 256, 0, Spike(256, 60)), 120));
    CHECK(OnlyAt(c.Frame(512, 256, 1, Spike(256, 70)), 1000));
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);
  }

  TEST_CASE("~fbinshift: in a rendered ~pfft a single-bin spectrum shifted up and down lands "
            "on the right bin, in one pass and across passes, against DSP::stft (#1165)") {
    SampleRateGuard guard;
    struct setting {
      UInt size;
      UInt bin;
      double shift; // in bins
      bool viaInlet; // the shift as a number on inlet 2, not the argument
    };
    // 256: 128 bins, one pass: 40 up 13 and down 21, 30 down 3.4 (to 27,
    // turning as 26.6), and 10 down 25 through 0 Hz (folded to 15, the
    // conjugate). 2048: 1024 bins, eight passes of 128: 100 up 200 (pass 0
    // to 2), 900 down 450 (pass 7 to 3), 128 down 4 and 124 up 5.4 across
    // the boundary of passes 0 and 1. Every bin times the hop (size / 4) is
    // whole cycles, so the spike is a partial on its bin.
    const setting settings[] = {{256, 40, 13.0, false},    {256, 40, -21.0, true},
                                {256, 30, -3.4, false},    {256, 10, -25.0, true},
                                {2048, 100, 200.0, false}, {2048, 900, -450.0, false},
                                {2048, 128, -4.0, true},   {2048, 124, 5.4, false}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.bin);
      CAPTURE(s.shift);
      const float hz = Hz(s.shift, s.size);
      // [~fftin] bin index -> [~== bin] -> [~fbinshift hz] -> [~fftout]
      patcherImplementation p(1, nullptr);
      YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " 4");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_EQUAL, std::to_string(s.bin));
      YSE::pHandle* shifter =
          p.CreateObject(YSE::OBJ::D_FBINSHIFT, s.viaInlet ? "" : std::to_string(hz));
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, one, shifter, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, one, shifter, out})
        p.SetObjectContainer(h, pfft);
      p.Connect(adc, 0, pfft, 0);
      p.Connect(in, 2, one, 0);
      p.Connect(one, 0, shifter, 0);
      p.Connect(shifter, 0, out, 0);
      p.Connect(shifter, 1, out, 1);
      p.Connect(pfft, 0, dac, 0);
      if (s.viaInlet) shifter->SetFloatData(2, hz);
      const auto rendered = Render(p, 1, 64);

      // In closed form: 1 at bin + round(shift), turned by
      // 2 pi shift hop / size a frame from 0; below 0 Hz folded to the
      // conjugate.
      const long long target = static_cast<long long>(s.bin) + std::llround(s.shift);
      const bool folded = target < 0;
      const auto lands = static_cast<UInt>(folded ? -target : target);
      const double step = 2.0 * PI * s.shift / 4.0;
      const auto Expected = [&](UInt bin) {
        stft fx(s.size, 4);
        auto spectrum = [&](stftFrame& frame) {
          const double angle = Wrap(step * static_cast<double>(frame.index));
          const double sine = folded ? -std::sin(angle) : std::sin(angle);
          for (UInt k = 0; k < frame.count; ++k) {
            frame.real[k] = k == bin ? static_cast<float>(std::cos(angle)) : 0.f;
            frame.imaginary[k] = k == bin ? static_cast<float>(sine) : 0.f;
          }
        };
        return Reference(fx, spectrum, rendered[0].size());
      };
      const std::vector<float> expected = Expected(lands);
      const float peak = PeakOf(expected);
      CAPTURE(peak);
      CHECK(peak > 1e-6f);
      CHECK(WorstOf(rendered[0], expected) <= 1e-4f * peak);
      // Not a bin off either way.
      CHECK(WorstOf(rendered[0], Expected(lands + 1)) > 0.1f * peak);
      CHECK(WorstOf(rendered[0], Expected(lands - 1)) > 0.1f * peak);
    }
  }

  TEST_CASE("~fbinshift: a rich signal through a rendered ~fbinshift after a JSON round trip, "
            "with a shift on the inlet and clip, is DSP::stft with DSP::spectralFreqShift "
            "(#1165)") {
    SampleRateGuard guard;
    struct setting {
      UInt size;
      UInt overlap;
      const char* args;
      float shift; // the argument's, or the inlet's when not 0
      float inlet;
      bool clip;
    };
    const setting settings[] = {
        {1024, 4, "250", 250.f, 0.f, true},
        {2048, 8, "-180.5", -180.5f, 0.f, false},
        {256, 4, "0 4096", 0.f, 1234.f, false},
        {512, 4, "100", 100.f, -3000.f, true},
    };
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.args);
      std::string dump;
      {
        patcherImplementation src(1, nullptr);
        YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
        YSE::pHandle* pfft = src.CreateObject(
            YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " " + std::to_string(s.overlap));
        YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
        YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
        YSE::pHandle* shifter = src.CreateObject(YSE::OBJ::D_FBINSHIFT, s.args);
        YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
        for (YSE::pHandle* h : {adc, pfft, dac, in, shifter, out})
          REQUIRE(h != nullptr);
        for (YSE::pHandle* h : {in, shifter, out})
          src.SetObjectContainer(h, pfft);
        src.Connect(adc, 0, pfft, 0);
        src.Connect(in, 0, shifter, 0);
        src.Connect(in, 1, shifter, 1);
        src.Connect(shifter, 0, out, 0);
        src.Connect(shifter, 1, out, 1);
        src.Connect(pfft, 0, dac, 0);
        dump = src.DumpJSON();
      }
      patcherImplementation p(1, nullptr);
      p.ParseJSON(dump);
      CHECK(p.DumpJSON() == dump);
      YSE::pHandle* shifter = nullptr;
      for (unsigned int id = 0; id < 6; ++id) {
        YSE::pHandle* h = p.GetHandleFromID(id);
        if (h != nullptr && std::string(h->Type()) == "~fbinshift") shifter = h;
      }
      REQUIRE(shifter != nullptr);
      CHECK(shifter->GetParams() == s.args);
      const float hz = s.inlet != 0.f ? s.inlet : s.shift;
      if (s.inlet != 0.f) shifter->SetFloatData(2, s.inlet);
      shifter->SetListData(0, std::string("clip ") + (s.clip ? "1" : "0"));
      const auto rendered = Render(p, 1, 96);

      // DSP::stft on the ~pfft's frame: size / 2 bins, the Nyquist bin 0.
      spectralFreqShift fn(hz);
      fn.clip(s.clip);
      stft fx(s.size, s.overlap);
      auto shift = [&](stftFrame& frame) {
        stftFrame half = frame;
        half.count = s.size / 2;
        half.bins = s.size / 2;
        fn(half);
        frame.real[s.size / 2] = 0.f;
        frame.imaginary[s.size / 2] = 0.f;
      };
      const std::vector<float> expected = Reference(fx, shift, rendered[0].size());
      CHECK(PeakOf(expected) > 0.05f);
      CHECK(WorstOf(rendered[0], expected) < 1e-6f);
    }
  }

  TEST_CASE("~fbinshift: the arguments survive a DumpJSON / ParseJSON round trip (#1165)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FBINSHIFT, "-250.5 2048") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FBINSHIFT, "440") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FBINSHIFT, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    YSE::pHandle* c = dst.GetHandleFromID(2);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(std::string(a->Type()) == "~fbinshift");
    CHECK(a->GetParams() == "-250.5 2048");
    CHECK(b->GetParams() == "440");
    CHECK(c->GetParams() == "");
  }

  TEST_CASE("~fbinshift: rendering, numbers and messages on the audio thread allocate nothing "
            "(#1165)") {
    SampleRateGuard guard;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx 2048 4");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* shifter = p.CreateObject(YSE::OBJ::D_FBINSHIFT, "333");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    for (YSE::pHandle* h : {adc, pfft, dac, in, shifter, out})
      REQUIRE(h != nullptr);
    for (YSE::pHandle* h : {in, shifter, out})
      p.SetObjectContainer(h, pfft);
    p.Connect(adc, 0, pfft, 0);
    p.Connect(in, 0, shifter, 0);
    p.Connect(in, 1, shifter, 1);
    p.Connect(shifter, 0, out, 0);
    p.Connect(shifter, 1, out, 1);
    p.Connect(pfft, 0, dac, 0);
    Render(p, 1, 32); // warm up
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    io[0] = 0.1f;
    rig r;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 64; ++i)
        p.ProcessAsInsert(io);
      r.shifter.GetInlet(0)->SetMessage("clip 1", YSE::T_DSP);
      r.shifter.GetInlet(2)->SetFloat(-500.f, YSE::T_DSP);
      r.shifter.ResetDSP();
      r.shifter.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
