// Tests for ~gizmo (patcher/spectral/dGizmo), issue #1164. The shifting is
// DSP::spectralPitchShift's and is tested in
// Tests/dsp/test_spectral_pitch_shift.cpp; these show:
//  - the shape, and that outside a ~pfft the real and imaginary parts pass
//    through unchanged, unconnected inlets reading their numbers;
//  - inside a ~pfft ~gizmo is frame-wide: it takes in every pass before it
//    puts out any, so a peak moved down into an earlier pass lands there;
//    the ratio from the argument, a number on inlet 2, a signal there, and
//    clip / freqshift;
//  - in a rendered ~pfft a single-bin spectrum shifted up and down lands on
//    the right bin, with the right angle frame after frame, at FFT size 256
//    (one pass) and 2048 (eight passes, the peak crossing pass boundaries),
//    against DSP::stft in closed form;
//  - a rich signal through a rendered ~gizmo after a JSON round trip, with
//    freqshift and clip, is DSP::stft with DSP::spectralPitchShift;
//  - the arguments survive a JSON round trip and rendering allocates
//    nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/spectralPitchShift.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dGizmo.h"
#include "patcher/spectral/spectralPass.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::spectralPitchShift;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::dGizmo;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  double Wrap(double a) {
    return a - 2.0 * PI * std::floor(a / (2.0 * PI) + 0.5);
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

  // A standalone ~gizmo with a sink on each outlet, driven pass by pass.
  struct rig {
    BufferSink re;
    BufferSink im;
    dGizmo gizmo;
    rig() {
      TestHelpers::Wire(gizmo, 0, re);
      TestHelpers::Wire(gizmo, 1, im);
    }

    // Every pass of a frame of `bins` bins consumed, real part `real`
    // (frame-long), imaginary unconnected; then every pass emitted, the
    // frame's output returned.
    std::vector<float> Frame(UInt size, UInt bins, U64 frame, const std::vector<float>& real,
                             std::vector<float>* imaginary = nullptr, buffer* ratio = nullptr) {
      buffer in(N);
      for (UInt p = 0; p < bins / N; ++p) {
        std::copy(real.begin() + p * N, real.begin() + (p + 1) * N, in.getPtr());
        const YSE::PATCHER::spectralPass pass = Pass(size, bins, p, frame, false);
        const YSE::PATCHER::spectralPassScope scope(&pass);
        gizmo.ResetDSP();
        gizmo.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        if (ratio != nullptr) gizmo.GetInlet(2)->SetBuffer(ratio, YSE::T_GUI);
        re.received = nullptr;
        gizmo.Calculate(YSE::T_DSP);
        CHECK(re.received == nullptr);
      }
      std::vector<float> out;
      for (UInt p = 0; p < bins / N; ++p) {
        const YSE::PATCHER::spectralPass pass = Pass(size, bins, p, frame, true);
        const YSE::PATCHER::spectralPassScope scope(&pass);
        gizmo.ResetDSP();
        gizmo.Calculate(YSE::T_DSP);
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
  TEST_CASE("~gizmo: registry, category and shape (#1164)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_GIZMO);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~gizmo");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 2);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_GIZMO));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 2);
    CHECK(o->SpectralFrameWide());
  }

  TEST_CASE("~gizmo: outside a ~pfft the real and imaginary parts pass through; unconnected "
            "inlets read their numbers (#1164)") {
    rig r;
    r.gizmo.SetParams("2");
    buffer re(N);
    buffer im(N);
    for (unsigned int i = 0; i < N; ++i) {
      re.getPtr()[i] = std::sin(0.1f * static_cast<float>(i));
      im.getPtr()[i] = 0.25f * static_cast<float>(i);
    }
    r.gizmo.ResetDSP();
    r.gizmo.GetInlet(0)->SetBuffer(&re, YSE::T_GUI);
    r.gizmo.GetInlet(1)->SetBuffer(&im, YSE::T_GUI);
    r.gizmo.Calculate(YSE::T_DSP);
    REQUIRE(r.re.received != nullptr);
    REQUIRE(r.im.received != nullptr);
    CHECK(std::equal(re.getPtr(), re.getPtr() + N, r.re.received->getPtr()));
    CHECK(std::equal(im.getPtr(), im.getPtr() + N, r.im.received->getPtr()));
    CHECK(r.re.received->getLength() == N);

    rig u;
    u.gizmo.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    u.gizmo.GetInlet(1)->SetInt(-2, YSE::T_GUI);
    u.gizmo.ResetDSP();
    u.gizmo.Calculate(YSE::T_DSP);
    REQUIRE(u.re.received != nullptr);
    CHECK(u.re.received->getPtr()[0] == 0.5f);
    CHECK(u.re.received->getPtr()[N - 1] == 0.5f);
    CHECK(u.im.received->getPtr()[7] == -2.f);
  }

  TEST_CASE("~gizmo: inside a ~pfft it takes in the whole frame before it puts out any, so a "
            "peak moves down into an earlier pass; ratio, clip and freqshift (#1164)") {
    // 512: 256 bins in two passes. Bin 200 (pass 1) at ratio 0.5 lands on
    // 100 (pass 0), and frame 0 has angle 0.
    rig r;
    r.gizmo.SetParams("0.5");
    std::vector<float> imaginary;
    const std::vector<float> down = r.Frame(512, 256, 0, Spike(256, 200), &imaginary);
    CHECK(OnlyAt(down, 100));
    CHECK(down[100] == 1.f);
    CHECK(OnlyAt(imaginary, 1000));

    // The same as the class on the whole frame.
    spectralPitchShift fn(0.5f);
    std::vector<float> re = Spike(256, 200);
    std::vector<float> im(256, 0.f);
    const YSE::PATCHER::spectralPass whole = Pass(512, 256, 0, 0, false);
    stftFrame f = whole.Frame(re.data(), im.data());
    f.count = 256;
    fn(f);
    CHECK(re == down);

    // A number on inlet 2 overrides the argument from the next frame: bin
    // 40 * 3 = 120. Bin 40 * hop is whole cycles, so the angle advances by
    // 2 pi * 80 / 4 a frame: back to 0.
    r.gizmo.GetInlet(2)->SetFloat(3.f, YSE::T_GUI);
    CHECK(OnlyAt(r.Frame(512, 256, 1, Spike(256, 40)), 120));
    std::vector<float> up = r.Frame(512, 256, 2, Spike(256, 40));
    CHECK(OnlyAt(up, 120));
    CHECK(up[120] == doctest::Approx(1.f));

    // A signal on inlet 2, read at the frame's first bin: 1.25 there, the
    // rest of the pass ignored.
    rig s;
    buffer ratio(N);
    ratio = 9.f;
    ratio.getPtr()[0] = 1.25f;
    CHECK(OnlyAt(s.Frame(512, 256, 0, Spike(256, 160), nullptr, &ratio), 200));

    // freqshift in Hz at the live rate; clip drops what folding would bring
    // back. 48 kHz / 512 = 93.75 Hz a bin.
    const UInt saved = YSE::SAMPLERATE;
    YSE::SAMPLERATE = 48000;
    rig t;
    t.gizmo.GetInlet(0)->SetMessage("freqshift 937.5", YSE::T_GUI);
    CHECK(OnlyAt(t.Frame(512, 256, 0, Spike(256, 20)), 30));
    t.gizmo.GetInlet(0)->SetMessage("freqshift -2812.5", YSE::T_GUI);
    // 20 - 30 = -10: folded to 10 (the real part of a real spike stays).
    CHECK(OnlyAt(t.Frame(512, 256, 5, Spike(256, 20)), 10));
    t.gizmo.GetInlet(0)->SetMessage("clip 1", YSE::T_GUI);
    CHECK(OnlyAt(t.Frame(512, 256, 9, Spike(256, 20)), 1000));
    t.gizmo.GetInlet(0)->SetMessage("clip 0", YSE::T_GUI);
    t.gizmo.GetInlet(0)->SetMessage("clip", YSE::T_GUI); // no number: ignored
    t.gizmo.GetInlet(0)->SetMessage("transpose 4", YSE::T_GUI); // not a message: ignored
    CHECK(OnlyAt(t.Frame(512, 256, 13, Spike(256, 20)), 10));
    YSE::SAMPLERATE = saved;

    // bins 128: the frame cut to its first pass.
    rig c;
    c.gizmo.SetParams("2 128");
    CHECK(c.gizmo.GetParams() == "2 128");
    CHECK(OnlyAt(c.Frame(512, 256, 0, Spike(256, 60)), 120));
    CHECK(OnlyAt(c.Frame(512, 256, 1, Spike(256, 70)), 1000));
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);
  }

  TEST_CASE("~gizmo: in a rendered ~pfft a single-bin spectrum shifted up and down lands on the "
            "right bin, in one pass and across passes, against DSP::stft (#1164)") {
    struct setting {
      UInt size;
      UInt bin;
      float ratio;
      bool viaInlet; // the ratio as a number on inlet 2, not the argument
    };
    // 256: 128 bins, one pass. 2048: 1024 bins, eight passes of 128: 100 up
    // to 300 (pass 0 to 2), 900 down to 450 (pass 7 to 3), 128 down to 124
    // and 124 up to 129 across the boundary of passes 0 and 1. Every bin
    // times the hop (size / 4) is whole cycles, so the spike is a partial
    // on its bin.
    const setting settings[] = {{256, 40, 1.25f, false},     {256, 40, 0.65f, true},
                                {2048, 100, 3.f, false},     {2048, 900, 0.5f, false},
                                {2048, 128, 0.96875f, true}, {2048, 124, 1.04f, false}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.bin);
      CAPTURE(s.ratio);
      // [~fftin] bin index -> [~== bin] -> [~gizmo ratio] -> [~fftout]
      patcherImplementation p(1, nullptr);
      YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " 4");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_EQUAL, std::to_string(s.bin));
      YSE::pHandle* gizmo =
          p.CreateObject(YSE::OBJ::D_GIZMO, s.viaInlet ? "" : std::to_string(s.ratio));
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, one, gizmo, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, one, gizmo, out})
        p.SetObjectContainer(h, pfft);
      p.Connect(adc, 0, pfft, 0);
      p.Connect(in, 2, one, 0);
      p.Connect(one, 0, gizmo, 0);
      p.Connect(gizmo, 0, out, 0);
      p.Connect(gizmo, 1, out, 1);
      p.Connect(pfft, 0, dac, 0);
      if (s.viaInlet) gizmo->SetFloatData(2, s.ratio);
      const auto rendered = Render(p, 1, 64);

      // In closed form: 1 at round(ratio * bin), turned by
      // 2 pi (ratio * bin - bin) hop / size a frame from 0.
      const double target = static_cast<double>(s.ratio) * s.bin;
      const auto lands = static_cast<UInt>(std::lround(target));
      const double step = 2.0 * PI * (target - s.bin) / 4.0;
      const auto Expected = [&](UInt bin) {
        stft fx(s.size, 4);
        auto spectrum = [&](stftFrame& frame) {
          const double angle = Wrap(step * static_cast<double>(frame.index));
          for (UInt k = 0; k < frame.count; ++k) {
            frame.real[k] = k == bin ? static_cast<float>(std::cos(angle)) : 0.f;
            frame.imaginary[k] = k == bin ? static_cast<float>(std::sin(angle)) : 0.f;
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

  TEST_CASE("~gizmo: a rich signal through a rendered ~gizmo after a JSON round trip, with "
            "freqshift and clip, is DSP::stft with DSP::spectralPitchShift (#1164)") {
    struct setting {
      UInt size;
      UInt overlap;
      const char* args;
      float ratio;
      float shift;
      bool clip;
    };
    const setting settings[] = {
        {1024, 4, "1.5", 1.5f, 200.f, true},
        {2048, 8, "0.6", 0.6f, -150.f, false},
        {256, 4, "1.3 4096", 1.3f, 0.f, false},
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
        YSE::pHandle* gizmo = src.CreateObject(YSE::OBJ::D_GIZMO, s.args);
        YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
        for (YSE::pHandle* h : {adc, pfft, dac, in, gizmo, out})
          REQUIRE(h != nullptr);
        for (YSE::pHandle* h : {in, gizmo, out})
          src.SetObjectContainer(h, pfft);
        src.Connect(adc, 0, pfft, 0);
        src.Connect(in, 0, gizmo, 0);
        src.Connect(in, 1, gizmo, 1);
        src.Connect(gizmo, 0, out, 0);
        src.Connect(gizmo, 1, out, 1);
        src.Connect(pfft, 0, dac, 0);
        dump = src.DumpJSON();
      }
      patcherImplementation p(1, nullptr);
      p.ParseJSON(dump);
      CHECK(p.DumpJSON() == dump);
      YSE::pHandle* gizmo = nullptr;
      for (unsigned int id = 0; id < 6; ++id) {
        YSE::pHandle* h = p.GetHandleFromID(id);
        if (h != nullptr && std::string(h->Type()) == "~gizmo") gizmo = h;
      }
      REQUIRE(gizmo != nullptr);
      CHECK(gizmo->GetParams() == s.args);
      gizmo->SetListData(0, "freqshift " + std::to_string(s.shift));
      gizmo->SetListData(0, std::string("clip ") + (s.clip ? "1" : "0"));
      const auto rendered = Render(p, 1, 96);

      // DSP::stft on the ~pfft's frame: size / 2 bins, the Nyquist bin 0.
      spectralPitchShift fn(s.ratio);
      fn.frequencyShift(s.shift).clip(s.clip);
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

  TEST_CASE("~gizmo: the arguments survive a DumpJSON / ParseJSON round trip (#1164)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GIZMO, "0.75 2048") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GIZMO, "2") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GIZMO, "") != nullptr);
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
    CHECK(std::string(a->Type()) == "~gizmo");
    CHECK(a->GetParams() == "0.75 2048");
    CHECK(b->GetParams() == "2");
    CHECK(c->GetParams() == "");
  }

  TEST_CASE("~gizmo: rendering, numbers and messages on the audio thread allocate nothing "
            "(#1164)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx 2048 4");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* gizmo = p.CreateObject(YSE::OBJ::D_GIZMO, "1.5");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    for (YSE::pHandle* h : {adc, pfft, dac, in, gizmo, out})
      REQUIRE(h != nullptr);
    for (YSE::pHandle* h : {in, gizmo, out})
      p.SetObjectContainer(h, pfft);
    p.Connect(adc, 0, pfft, 0);
    p.Connect(in, 0, gizmo, 0);
    p.Connect(in, 1, gizmo, 1);
    p.Connect(gizmo, 0, out, 0);
    p.Connect(gizmo, 1, out, 1);
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
      r.gizmo.GetInlet(0)->SetMessage("freqshift 100", YSE::T_DSP);
      r.gizmo.GetInlet(0)->SetMessage("clip 1", YSE::T_DSP);
      r.gizmo.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      r.gizmo.ResetDSP();
      r.gizmo.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
