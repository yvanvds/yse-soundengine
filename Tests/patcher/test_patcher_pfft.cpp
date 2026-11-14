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
//    round trip, and rendering allocates nothing.

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
}
