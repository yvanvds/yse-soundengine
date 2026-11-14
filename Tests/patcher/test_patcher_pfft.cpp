// Tests for ~pfft, ~fftin, ~fftout and ~fftinfo (patcher/spectral/dPfft),
// issue #1041. The transform is DSP::stft's halves and is tested in
// Tests/dsp/test_stft.cpp; these build patches the way a patch author does —
// objects placed inside a ~pfft with SetContainer, cords drawn inside and to
// the ~pfft's own pins — render them through ProcessAsInsert (an ~adc fed by
// the host buffer, a ~dac read back), and check:
//  - an identity subpatch gives the input back `size` samples later;
//  - a spectral brick-wall made of ordinary objects on the bin-index outlet
//    (~< and ~*) matches DSP::stft with the same callback, bit for bit, at an
//    FFT size whose frame takes several passes, and at a hop below the block;
//  - inlets and outlets are routed by index, unconnected inlets read their
//    numbers, ~fftinfo reports the frame, mute and clear work;
//  - a signal cord across the boundary is refused, the patch survives a JSON
//    round trip, and rendering allocates nothing;
//  - with a frame-wide object inside (issue #1280, ~framesmooth's centred
//    window) the subpatch renders in stages: a centred smooth of a
//    single-bin spectrum is symmetric around the bin at one pass and across
//    pass boundaries, a cord that skips a stage is carried, and each
//    ~fftout is collected in its own stage, against DSP::stft.

#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dPfft.h"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int BLOCK = STANDARD_BUFFERSIZE;

  bool Listed(const char* name) {
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == name) return true;
    return false;
  }

  // Sines well below Nyquist: a half-spectrum ~pfft drops the Nyquist bin,
  // which these leave all but empty.
  float Tone(std::size_t n, unsigned int channel = 0) {
    const double t = static_cast<double>(n);
    return static_cast<float>(0.4 * std::sin(0.05 * t + channel) +
                              0.3 * std::sin(0.31 * t + 0.5 * channel) + 0.2 * std::cos(0.9 * t));
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

  // `blocks` blocks through the patcher as an insert; channel c of the
  // input is in(n, c), the outputs are returned per channel.
  template <class In>
  std::vector<std::vector<float>> Render(patcherImplementation& p, unsigned int channels,
                                         unsigned int blocks, In in, std::size_t& at) {
    std::vector<std::vector<float>> out(channels);
    MULTICHANNELBUFFER io(channels);
    for (auto& b : io)
      b.resize(BLOCK);
    for (unsigned int block = 0; block < blocks; ++block) {
      for (unsigned int c = 0; c < channels; ++c)
        for (unsigned int i = 0; i < BLOCK; ++i)
          io[c].getPtr()[i] = in(at + i, c);
      p.ProcessAsInsert(io);
      for (unsigned int c = 0; c < channels; ++c)
        for (unsigned int i = 0; i < BLOCK; ++i)
          out[c].push_back(io[c].getPtr()[i]);
      at += BLOCK;
    }
    return out;
  }

  // [~adc] -> [~pfft args] -> [~dac], and an identity subpatch inside:
  // [~fftin 0 window] real / imaginary -> [~fftout 0 window].
  struct identity {
    YSE::pHandle* adc;
    YSE::pHandle* pfft;
    YSE::pHandle* dac;
    YSE::pHandle* in;
    YSE::pHandle* out;
  };

  identity BuildIdentity(patcherImplementation& p, const std::string& args,
                         const std::string& inArgs = "0", const std::string& outArgs = "0") {
    identity r{};
    r.adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    r.pfft = p.CreateObject(YSE::OBJ::D_PFFT, args);
    r.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    r.in = p.CreateObject(YSE::OBJ::D_FFTIN, inArgs);
    r.out = p.CreateObject(YSE::OBJ::D_FFTOUT, outArgs);
    REQUIRE(r.adc != nullptr);
    REQUIRE(r.pfft != nullptr);
    REQUIRE(r.dac != nullptr);
    REQUIRE(r.in != nullptr);
    REQUIRE(r.out != nullptr);
    p.SetObjectContainer(r.in, r.pfft);
    p.SetObjectContainer(r.out, r.pfft);
    p.Connect(r.adc, 0, r.pfft, 0);
    p.Connect(r.in, 0, r.out, 0);
    p.Connect(r.in, 1, r.out, 1);
    p.Connect(r.pfft, 0, r.dac, 0);
    return r;
  }

  float WorstDelayed(const std::vector<float>& out, unsigned int delay,
                     float (*in)(std::size_t, unsigned int), unsigned int channel = 0,
                     std::size_t from = 0) {
    float worst = 0.f;
    for (std::size_t n = from; n < out.size(); ++n) {
      const float expected = n >= delay ? in(n - delay, channel) : 0.f;
      worst = std::max(worst, std::abs(out[n] - expected));
    }
    return worst;
  }

  float ToneIn(std::size_t n, unsigned int c) {
    return Tone(n, c);
  }

  // The brick-wall patch: bins below `cutoff` pass, the rest are zeroed, by
  // ordinary objects on the ~fftin bin index.
  void BuildBrickwall(patcherImplementation& p, const std::string& args, int cutoff) {
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, args);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* below = p.CreateObject(YSE::OBJ::D_LESS, std::to_string(cutoff));
    YSE::pHandle* re = p.CreateObject(YSE::OBJ::D_MULTIPLY, "");
    YSE::pHandle* im = p.CreateObject(YSE::OBJ::D_MULTIPLY, "");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    for (YSE::pHandle* h : {in, below, re, im, out}) {
      REQUIRE(h != nullptr);
      p.SetObjectContainer(h, pfft);
    }
    p.Connect(adc, 0, pfft, 0);
    p.Connect(in, 2, below, 0);
    p.Connect(in, 0, re, 0);
    p.Connect(below, 0, re, 1);
    p.Connect(in, 1, im, 0);
    p.Connect(below, 0, im, 1);
    p.Connect(re, 0, out, 0);
    p.Connect(im, 0, out, 1);
    p.Connect(pfft, 0, dac, 0);
  }

  float RichIn(std::size_t n, unsigned int) {
    return Rich(n);
  }

  // `length` samples of `in` through DSP::stft in blocks, `f` called for
  // every frame: what a ~pfft with the same settings must put out.
  template <class F>
  std::vector<float> Reference(stft& fx, F& f, float (*in)(std::size_t, unsigned int),
                               std::size_t length) {
    std::vector<float> out;
    YSE::DSP::buffer b(BLOCK);
    for (std::size_t start = 0; start < length; start += BLOCK) {
      for (unsigned int i = 0; i < BLOCK; ++i)
        b.getPtr()[i] = in(start + i, 0);
      YSE::DSP::buffer& o = fx(b, f);
      for (unsigned int i = 0; i < BLOCK; ++i)
        out.push_back(o.getPtr()[i]);
    }
    return out;
  }

  // DSP::stft with the brick-wall callback (the Nyquist bin, which a
  // half-spectrum ~pfft frame does not hold, zeroed as well).
  std::vector<float> ReferenceBrickwall(UInt size, UInt overlap, UInt cutoff, std::size_t length) {
    stft fx(size, overlap);
    auto wall = [&](stftFrame& frame) {
      for (UInt k = 0; k < frame.count; ++k) {
        if (k < cutoff && k < size / 2) continue;
        frame.real[k] = 0.f;
        frame.imaginary[k] = 0.f;
      }
    };
    return Reference(fx, wall, RichIn, length);
  }

  std::size_t Differences(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t differ = 0;
    for (std::size_t n = 0; n < a.size(); ++n)
      if (a[n] != b[n]) differ++;
    return differ;
  }

  // ~framesmooth's centred window in closed form (issue #1280): the mean of
  // x[k] and the values s / 2 below and s - 1 - s / 2 above it, cut to the
  // frame.
  std::vector<double> CentredSmooth(const std::vector<double>& x, UInt s) {
    std::vector<double> out(x.size());
    const std::size_t before = s / 2;
    for (std::size_t k = 0; k < x.size(); ++k) {
      const std::size_t from = k > before ? k - before : 0;
      const std::size_t to = std::min(x.size(), k + s - before);
      double sum = 0.0;
      for (std::size_t j = from; j < to; ++j)
        sum += x[j];
      out[k] = sum / static_cast<double>(to - from);
    }
    return out;
  }

  float WorstOf(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0) {
    REQUIRE(a.size() == b.size());
    float worst = 0.f;
    for (std::size_t n = from; n < a.size(); ++n)
      worst = std::max(worst, std::abs(a[n] - b[n]));
    return worst;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~pfft / ~fftin / ~fftout / ~fftinfo: registry, category and shape (#1041)") {
    for (const char* name :
         {YSE::OBJ::D_PFFT, YSE::OBJ::D_FFTIN, YSE::OBJ::D_FFTOUT, YSE::OBJ::D_FFTINFO}) {
      CAPTURE(name);
      CHECK(Listed(name));
    }
    CHECK(std::string(YSE::OBJ::D_PFFT) == "~pfft");
    CHECK(std::string(YSE::OBJ::D_FFTIN) == "~fftin");
    CHECK(std::string(YSE::OBJ::D_FFTOUT) == "~fftout");
    CHECK(std::string(YSE::OBJ::D_FFTINFO) == "~fftinfo");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "");
    YSE::pHandle* wide = p.CreateObject(YSE::OBJ::D_PFFT, "fx 1024 4 0 1 3 2");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "");
    YSE::pHandle* info = p.CreateObject(YSE::OBJ::D_FFTINFO, "");
    REQUIRE(pfft != nullptr);
    REQUIRE(wide != nullptr);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    REQUIRE(info != nullptr);
    CHECK(pfft->GetInputs() == 1);
    CHECK(pfft->IsDSPInput(0));
    CHECK(pfft->GetOutputs() == 1);
    CHECK(pfft->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(wide->GetInputs() == 3);
    CHECK(wide->GetOutputs() == 2);
    CHECK(in->GetInputs() == 0);
    CHECK(in->GetOutputs() == 3);
    CHECK(out->GetInputs() == 2);
    CHECK(out->IsDSPInput(0));
    CHECK(out->IsDSPInput(1));
    CHECK(out->GetOutputs() == 0);
    CHECK(info->GetInputs() == 1);
    CHECK(info->GetOutputs() == 4);
    CHECK(info->OutputDataType(0) == YSE::OUT_TYPE::INT);

    auto* host = static_cast<YSE::PATCHER::dPfft*>(p.GetObjectFromID(wide->GetID()));
    CHECK(host->Size() == 1024);
    CHECK(host->Hop() == 256);
    CHECK(host->Bins() == 1024);
    CHECK(host->FullSpectrum());
    auto* def = static_cast<YSE::PATCHER::dPfft*>(p.GetObjectFromID(pfft->GetID()));
    CHECK(def->Size() == 512);
    CHECK(def->Hop() == 256);
    CHECK(def->Bins() == 256);
    for (YSE::pHandle* h : {pfft, in, out, info})
      CHECK(p.GetObjectFromID(h->GetID())->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);

    // A ~pfft holds objects, as a patcher does.
    p.SetObjectContainer(in, pfft);
    CHECK(p.GetObjectContainer(in) == pfft);
  }

  TEST_CASE("~pfft: an identity subpatch is DSP::stft's identity; the input comes back size "
            "samples later (#1041)") {
    using W = YSE::DSP::stftWindow;
    struct setting {
      const char* args;
      const char* in;
      const char* out;
      UInt size;
      UInt overlap;
      UInt offset;
      bool full;
      W::TYPE analysis;
      W::TYPE synthesis;
      float tolerance; // the half spectrum's missing Nyquist part, past the start
    };
    // Default windows (Hann both sides) at overlaps 2 and 4, a square
    // synthesis window, the full spectrum, a hop below the block (64 / 4 =
    // 16: eight frames a block), an offset, frames of 8 passes.
    const setting settings[] = {
        {"fx", "0", "0", 512, 2, 0, false, W::HANNING, W::HANNING, 1e-4f},
        {"fx 256 4", "0", "0", 256, 4, 0, false, W::HANNING, W::HANNING, 1e-4f},
        {"fx 256 2", "0 hanning", "0 square", 256, 2, 0, false, W::HANNING, W::SQUARE, 1e-4f},
        {"fx 128 2 0 1", "0", "0", 128, 2, 0, true, W::HANNING, W::HANNING, 2e-5f},
        {"fx 64 4", "0 hamming", "0 hamming", 64, 4, 0, false, W::HAMMING, W::HAMMING, 2e-3f},
        {"fx 512 4 37", "0", "0", 512, 4, 37, false, W::HANNING, W::HANNING, 1e-4f},
        {"fx 2048 4", "0 blackman", "0 blackman", 2048, 4, 0, false, W::BLACKMAN, W::BLACKMAN,
         1e-4f},
    };
    for (const setting& s : settings) {
      CAPTURE(std::string(s.args));
      CAPTURE(std::string(s.in));
      CAPTURE(std::string(s.out));
      patcherImplementation p(1, nullptr);
      BuildIdentity(p, s.args, s.in, s.out);
      std::size_t at = 0;
      const auto out = Render(p, 1, 48, ToneIn, at);

      // Bit for bit what DSP::stft gives with the same settings, the
      // Nyquist bin left out of a half spectrum as the ~pfft frame leaves it.
      stft fx(s.size, s.overlap, s.full, s.offset);
      fx.window(s.analysis, s.synthesis);
      const UInt nyquist = s.size / 2;
      auto drop = [&](stftFrame& frame) {
        if (frame.fullSpectrum) return;
        frame.real[nyquist] = 0.f;
        frame.imaginary[nyquist] = 0.f;
      };
      CHECK(Differences(out[0], Reference(fx, drop, ToneIn, out[0].size())) == 0);

      // Which is the input, size samples later: exactly with the full
      // spectrum; with a half spectrum, without the frames' Nyquist part,
      // which these tones all but lack once past the start (the input steps
      // up at sample 0) — least so under the Hamming window, whose
      // sidelobes fall slowest.
      CHECK(WorstDelayed(out[0], s.size, ToneIn, 0, s.full ? 0 : 2 * s.size + s.offset) <
            s.tolerance);
    }
  }

  TEST_CASE("~pfft: a brick-wall made of ~< and ~* on the bin index matches DSP::stft, in "
            "several passes and at a hop below the block (#1041)") {
    struct setting {
      UInt size;
      UInt overlap;
      int cutoff;
    };
    // 1024: 512 bins, four passes, the cutoff in the third. 64 at overlap 4:
    // 32 bins in one pass, a frame every 16 samples.
    const setting settings[] = {{1024, 4, 300}, {64, 4, 9}, {256, 2, 50}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      CAPTURE(s.cutoff);
      patcherImplementation p(1, nullptr);
      BuildBrickwall(p, "wall " + std::to_string(s.size) + " " + std::to_string(s.overlap),
                     s.cutoff);
      std::size_t at = 0;
      const auto out = Render(p, 1, 40, RichIn, at);
      const std::vector<float> expected =
          ReferenceBrickwall(s.size, s.overlap, static_cast<UInt>(s.cutoff), out[0].size());
      CHECK(Differences(out[0], expected) == 0);
      // And it is not the identity: the wall took something out.
      float removed = 0.f;
      for (std::size_t n = s.size; n < expected.size(); ++n)
        removed = std::max(removed, std::abs(out[0][n] - Rich(n - s.size)));
      CHECK(removed > 0.01f);
    }
  }

  TEST_CASE("~pfft: inlets and outlets are routed by the ~fftin / ~fftout index (#1041)") {
    // Two inlets, two outlets, crossed inside: ~fftin 0 -> ~fftout 1 and
    // ~fftin 1 -> ~fftout 0.
    patcherImplementation p(2, nullptr);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "x 256 4 0 0 2 2");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in0 = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* in1 = p.CreateObject(YSE::OBJ::D_FFTIN, "1");
    YSE::pHandle* out0 = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    YSE::pHandle* out1 = p.CreateObject(YSE::OBJ::D_FFTOUT, "1");
    for (YSE::pHandle* h : {in0, in1, out0, out1})
      p.SetObjectContainer(h, pfft);
    p.Connect(adc, 0, pfft, 0);
    p.Connect(adc, 1, pfft, 1);
    p.Connect(in0, 0, out1, 0);
    p.Connect(in0, 1, out1, 1);
    p.Connect(in1, 0, out0, 0);
    p.Connect(in1, 1, out0, 1);
    p.Connect(pfft, 0, dac, 0);
    p.Connect(pfft, 1, dac, 1);
    std::size_t at = 0;
    const auto out = Render(p, 2, 24, ToneIn, at);
    // Past the start, where the input steps up from 0 (see the identity
    // case).
    CHECK(WorstDelayed(out[0], 256, ToneIn, 1, 512) < 1e-4f);
    CHECK(WorstDelayed(out[1], 256, ToneIn, 0, 512) < 1e-4f);
  }

  TEST_CASE("~pfft: an unconnected inlet reads its number; mute and clear (#1041)") {
    patcherImplementation p(1, nullptr);
    identity r = BuildIdentity(p, "fx 256 4");
    p.Disconnect(r.adc, 0, r.pfft, 0);
    r.pfft->SetFloatData(0, 0.5f);
    std::size_t at = 0;
    const auto zero = [](std::size_t, unsigned int) { return 0.f; };
    auto out = Render(p, 1, 8, zero, at);
    // 0 for the first frame's worth, then the constant, 256 samples later.
    // Near the start the frames hold the step up from 0, whose Nyquist part
    // a half spectrum leaves out; a frame later, the constant exactly.
    for (std::size_t n = 0; n < 256; ++n)
      CHECK(std::abs(out[0][n]) < 1e-3f);
    for (std::size_t n = 512; n < out[0].size(); ++n)
      CHECK(out[0][n] == doctest::Approx(0.5).epsilon(1e-5));

    // mute: what is in flight drains within a frame, then silence.
    r.pfft->SetListData(0, "mute 1");
    out = Render(p, 1, 6, zero, at);
    for (std::size_t n = 256; n < out[0].size(); ++n)
      CHECK(out[0][n] == 0.f);
    // mute 0: the constant again, a frame later.
    r.pfft->SetListData(0, "mute 0");
    out = Render(p, 1, 6, zero, at);
    for (std::size_t n = 256; n < out[0].size(); ++n)
      CHECK(out[0][n] == doctest::Approx(0.5).epsilon(1e-5));
    // clear: the stream starts again from silence.
    r.pfft->SetFloatData(0, 0.f);
    r.pfft->SetListData(0, "clear");
    out = Render(p, 1, 4, zero, at);
    for (float v : out[0])
      CHECK(v == 0.f);
  }

  TEST_CASE("~fftinfo: reports size, frame size, hop and the full-spectrum flag (#1041)") {
    struct setting {
      const char* args;
      const char* expected[4];
    };
    const setting settings[] = {
        {"fx 1024 4", {"1024", "512", "256", "0"}},
        {"fx 64 2 0 1", {"64", "64", "32", "1"}},
    };
    for (const setting& s : settings) {
      CAPTURE(std::string(s.args));
      patcherImplementation p(1, nullptr);
      identity r = BuildIdentity(p, s.args);
      YSE::pHandle* info = p.CreateObject(YSE::OBJ::D_FFTINFO, "");
      REQUIRE(info != nullptr);
      p.SetObjectContainer(info, r.pfft);
      YSE::pHandle* boxes[4];
      for (int i = 0; i < 4; ++i) {
        boxes[i] = p.CreateObject(YSE::OBJ::G_INT, "");
        REQUIRE(boxes[i] != nullptr);
        // A control cord out of the subpatch is fine: only signal cords
        // cannot cross.
        p.Connect(info, i, boxes[i], 0);
      }
      std::size_t at = 0;
      Render(p, 1, 10, ToneIn, at);
      for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(boxes[i]->GetGuiValue() == s.expected[i]);
      }
      // A bang reports again.
      for (int i = 0; i < 4; ++i)
        boxes[i]->SetIntData(0, -1);
      info->SetBang(0);
      for (int i = 0; i < 4; ++i)
        CHECK(boxes[i]->GetGuiValue() == s.expected[i]);
    }
    // Outside a ~pfft it reports nothing.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* info = p.CreateObject(YSE::OBJ::D_FFTINFO, "");
    YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_INT, "");
    p.Connect(info, 0, box, 0);
    box->SetIntData(0, -1);
    std::size_t at = 0;
    Render(p, 1, 4, ToneIn, at);
    info->SetBang(0);
    CHECK(box->GetGuiValue() == "-1");
  }

  TEST_CASE("~pfft: a signal cord across the boundary is refused and a moved one is inert "
            "(#1041)") {
    patcherImplementation p(1, nullptr);
    identity r = BuildIdentity(p, "fx 256 4");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    REQUIRE(sig != nullptr);
    // Into the subpatch, and out of it.
    p.Connect(sig, 0, r.out, 0);
    CHECK(p.OutletConnections(sig, 0) == 0);
    p.Connect(r.in, 0, r.dac, 0);
    CHECK(p.OutletConnections(r.in, 0) == 1); // the ~fftout cord only

    // A cord made first and crossed by a later move is not compiled: the
    // ~sig moved inside a ~pfft no longer reaches the ~dac outside.
    patcherImplementation q(1, nullptr);
    YSE::pHandle* pfft = q.CreateObject(YSE::OBJ::D_PFFT, "fx 256 4");
    YSE::pHandle* moved = q.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* dac = q.CreateObject(YSE::OBJ::D_DAC, "");
    q.Connect(moved, 0, dac, 0);
    std::size_t at = 0;
    const auto zero = [](std::size_t, unsigned int) { return 0.f; };
    auto out = Render(q, 1, 1, zero, at);
    CHECK(out[0][0] == doctest::Approx(0.25));
    q.SetObjectContainer(moved, pfft);
    out = Render(q, 1, 1, zero, at);
    CHECK(out[0][0] == 0.f);
  }

  TEST_CASE("~pfft: the patch survives a DumpJSON / ParseJSON round trip (#1041)") {
    std::string dump;
    std::vector<float> before;
    {
      patcherImplementation src(1, nullptr);
      BuildBrickwall(src, "wall 512 4 0 0 1 1", 100);
      std::size_t at = 0;
      before = Render(src, 1, 24, RichIn, at)[0];
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    std::size_t at = 0;
    const std::vector<float> after = Render(dst, 1, 24, RichIn, at)[0];
    CHECK(after == before);
    CHECK(dst.DumpJSON() == dump);
  }

  TEST_CASE("~pfft: rendering, numbers and messages on the audio thread allocate nothing "
            "(#1041)") {
    patcherImplementation p(1, nullptr);
    BuildBrickwall(p, "wall 1024 8", 200);
    MULTICHANNELBUFFER io(1);
    io[0].resize(BLOCK);
    io[0] = 0.1f;
    for (int i = 0; i < 16; ++i)
      p.ProcessAsInsert(io); // warm up
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 64; ++i)
        p.ProcessAsInsert(io);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("~pfft: a frame-wide ~framesmooth gets a stage of its own; its centred smooth of a "
            "single-bin spectrum is symmetric in one pass and across passes (#1280)") {
    struct setting {
      UInt size;
      int bin;
      UInt smoothness;
    };
    // 256: 128 bins, one pass. 2048: 1024 bins, eight passes; bins 127 and
    // 128 have their windows straddle the boundary of passes 0 and 1, bin
    // 511 a 41-bin window across that of passes 3 and 4; bins 2 and 1021 are
    // cut by the ends of the frame.
    const setting settings[] = {{256, 60, 5},    {256, 60, 41}, {2048, 127, 5}, {2048, 128, 5},
                                {2048, 511, 41}, {2048, 2, 5},  {2048, 1021, 9}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.bin);
      CAPTURE(s.smoothness);
      // [~fftin] bin index -> [~== bin] -> [~framesmooth s] -> [~fftout]
      // real: a spectrum of 1 at one bin, smoothed, the imaginary part 0.
      patcherImplementation p(1, nullptr);
      YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " 4");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_EQUAL, std::to_string(s.bin));
      YSE::pHandle* smooth = p.CreateObject(YSE::OBJ::D_FRAMESMOOTH, std::to_string(s.smoothness));
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, one, smooth, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, one, smooth, out})
        p.SetObjectContainer(h, pfft);
      p.Connect(adc, 0, pfft, 0);
      p.Connect(in, 2, one, 0);
      p.Connect(one, 0, smooth, 0);
      p.Connect(smooth, 0, out, 0);
      p.Connect(pfft, 0, dac, 0);
      std::size_t at = 0;
      const auto rendered = Render(p, 1, 48, RichIn, at);

      // The spectrum, in closed form: 1 / (window) on the bins whose window
      // holds `bin`, symmetric around it where the frame does not cut it.
      const UInt half = s.size / 2;
      std::vector<double> spike(half, 0.0);
      spike[static_cast<std::size_t>(s.bin)] = 1.0;
      const std::vector<double> smoothed = CentredSmooth(spike, s.smoothness);
      const std::size_t reach = s.smoothness / 2;
      const auto b = static_cast<std::size_t>(s.bin);
      if (b > reach && b + reach + 1 < half && s.smoothness % 2 == 1) {
        for (std::size_t d = 0; d <= reach + 1; ++d)
          REQUIRE(smoothed[b - d] == smoothed[b + d]);
      }
      stft fx(s.size, 4);
      auto spectrum = [&](stftFrame& frame) {
        for (UInt k = 0; k < frame.count; ++k) {
          frame.real[k] = k < half ? static_cast<float>(smoothed[k]) : 0.f;
          frame.imaginary[k] = 0.f;
        }
      };
      const std::vector<float> expected = Reference(fx, spectrum, RichIn, rendered[0].size());
      // A quiet sound (one bin, divided by the size), so measured against
      // its own peak.
      float peak = 0.f;
      for (const float v : expected)
        peak = std::max(peak, std::abs(v));
      CHECK(peak > 1e-6f);
      CAPTURE(peak);
      CHECK(WorstOf(rendered[0], expected) <= 1e-4f * peak);
    }
  }

  TEST_CASE("~pfft: cords that skip a stage are carried, and every ~fftout is collected in "
            "its own stage (#1280)") {
    // Three outlets, after a JSON round trip:
    //  0: a magnitude smoother, ~cartopol -> ~framesmooth 9 -> ~poltocar,
    //     the phase from ~cartopol (stage 0) carried into ~poltocar's stage 1;
    //  1: an identity, ~fftin -> ~fftout 1, collected in stage 0;
    //  2: two frame-wide objects in a row, the real part through
    //     ~framesmooth 5 -> ~framesmooth 3 (stage 2), the imaginary part
    //     from ~fftin carried from stage 0 into stage 2.
    constexpr UInt SIZE = 1024;
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = src.CreateObject(YSE::OBJ::D_PFFT, "fx 1024 4 0 0 1 3");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* polar = src.CreateObject(YSE::OBJ::D_CARTOPOL, "");
      YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_FRAMESMOOTH, "9");
      YSE::pHandle* cart = src.CreateObject(YSE::OBJ::D_POLTOCAR, "");
      YSE::pHandle* out0 = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      YSE::pHandle* out1 = src.CreateObject(YSE::OBJ::D_FFTOUT, "1");
      YSE::pHandle* first = src.CreateObject(YSE::OBJ::D_FRAMESMOOTH, "5");
      YSE::pHandle* second = src.CreateObject(YSE::OBJ::D_FRAMESMOOTH, "3");
      YSE::pHandle* out2 = src.CreateObject(YSE::OBJ::D_FFTOUT, "2");
      for (YSE::pHandle* h :
           {adc, pfft, dac, in, polar, smooth, cart, out0, out1, first, second, out2})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, polar, smooth, cart, out0, out1, first, second, out2})
        src.SetObjectContainer(h, pfft);
      src.Connect(adc, 0, pfft, 0);
      src.Connect(in, 0, polar, 0);
      src.Connect(in, 1, polar, 1);
      src.Connect(polar, 0, smooth, 0);
      src.Connect(smooth, 0, cart, 0);
      src.Connect(polar, 1, cart, 1);
      src.Connect(cart, 0, out0, 0);
      src.Connect(cart, 1, out0, 1);
      src.Connect(in, 0, out1, 0);
      src.Connect(in, 1, out1, 1);
      src.Connect(in, 0, first, 0);
      src.Connect(first, 0, second, 0);
      src.Connect(second, 0, out2, 0);
      src.Connect(in, 1, out2, 1);
      for (int o = 0; o < 3; ++o)
        src.Connect(pfft, o, dac, o);
      dump = src.DumpJSON();
    }
    patcherImplementation p(3, nullptr);
    p.ParseJSON(dump);
    CHECK(p.DumpJSON() == dump);
    std::size_t at = 0;
    const auto in3 = [](std::size_t n, unsigned int) { return Rich(n); };
    const auto rendered = Render(p, 3, 48, in3, at);
    const std::size_t length = rendered[0].size();

    const UInt half = SIZE / 2;
    stft polarFx(SIZE, 4);
    auto magnitudes = [&](stftFrame& frame) {
      std::vector<double> mag(half);
      std::vector<double> phase(half);
      for (UInt k = 0; k < half; ++k) {
        mag[k] = std::hypot(static_cast<double>(frame.real[k]), frame.imaginary[k]);
        phase[k] = std::atan2(static_cast<double>(frame.imaginary[k]), frame.real[k]);
      }
      const std::vector<double> smoothed = CentredSmooth(mag, 9);
      for (UInt k = 0; k < frame.count; ++k) {
        frame.real[k] = k < half ? static_cast<float>(smoothed[k] * std::cos(phase[k])) : 0.f;
        frame.imaginary[k] = k < half ? static_cast<float>(smoothed[k] * std::sin(phase[k])) : 0.f;
      }
    };
    stft identityFx(SIZE, 4);
    auto identity = [&](stftFrame& frame) {
      for (UInt k = half; k < frame.count; ++k) {
        frame.real[k] = 0.f;
        frame.imaginary[k] = 0.f;
      }
    };
    stft twiceFx(SIZE, 4);
    auto twice = [&](stftFrame& frame) {
      std::vector<double> re(frame.real, frame.real + half);
      const std::vector<double> smoothed = CentredSmooth(CentredSmooth(re, 5), 3);
      for (UInt k = 0; k < frame.count; ++k) {
        frame.real[k] = k < half ? static_cast<float>(smoothed[k]) : 0.f;
        if (k >= half) frame.imaginary[k] = 0.f;
      }
    };
    const std::vector<float> expected0 = Reference(polarFx, magnitudes, RichIn, length);
    const std::vector<float> expected1 = Reference(identityFx, identity, RichIn, length);
    const std::vector<float> expected2 = Reference(twiceFx, twice, RichIn, length);
    CHECK(WorstOf(rendered[0], expected0) < 1e-4f);
    CHECK(Differences(rendered[1], expected1) == 0);
    CHECK(WorstOf(rendered[2], expected2) < 1e-5f);
    // None of them is the identity but outlet 1: the smoothing changed the
    // sound, the carried phase kept it a sound (with phases of 0 instead,
    // the magnitude smoother puts out something else entirely).
    CHECK(WorstOf(rendered[0], expected1, SIZE) > 0.01f);
    CHECK(WorstOf(rendered[2], expected1, SIZE) > 0.01f);

    // Rendering the staged patch allocates nothing.
    MULTICHANNELBUFFER io(3);
    for (auto& b : io) {
      b.resize(BLOCK);
      b = 0.1f;
    }
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 32; ++i)
        p.ProcessAsInsert(io);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
