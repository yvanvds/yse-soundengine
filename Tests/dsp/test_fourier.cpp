// Tests for YSE::DSP FFT classes and the underlying Mayer functions.
// No audio device required; all operations are purely mathematical.
//
// Normalization convention: mayer_fft / mayer_ifft are both unnormalized,
// so IFFT(FFT(x))[n] == N * x[n].  Tests that rely on the round-trip scale
// this accordingly or normalize before comparing.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/fourier/fft.hpp"
#include "dsp/fourier/mayer.h"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

TEST_SUITE("dsp") {

  // ─── fft ─────────────────────────────────────────────────────────────────────

  TEST_CASE("fft: zero input produces zero output") {
    YSE::DSP::fft f;
    YSE::DSP::buffer re = TestHelpers::makeBuffer(512, 0.0f);
    YSE::DSP::buffer im = TestHelpers::makeBuffer(512, 0.0f);
    f(re, im);
    float* rp = f.getReal().getPtr();
    float* ip = f.getImaginary().getPtr();
    for (unsigned i = 0; i < 512; ++i) {
      CHECK(std::abs(rp[i]) < 1e-6f);
      CHECK(std::abs(ip[i]) < 1e-6f);
    }
  }

  TEST_CASE("fft: peak bin matches input sine frequency") {
    // Choose an exact bin: k=10, N=512, SR=44100 → freq = 10*44100/512 ≈ 860.7 Hz.
    // Generating sin at this exact frequency aligns with the bin, avoiding leakage.
    const unsigned N = 512;
    const unsigned k = 10;
    const float SR = 44100.0f;
    const float freq = static_cast<float>(k) * SR / static_cast<float>(N);

    YSE::DSP::buffer re(N);
    float* rp = re.getPtr();
    for (unsigned i = 0; i < N; ++i)
      rp[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / SR);
    YSE::DSP::buffer im = TestHelpers::makeBuffer(N, 0.0f);

    YSE::DSP::fft f;
    f(re, im);

    unsigned peak = TestHelpers::peakBinIndex(f.getReal().getPtr(), f.getImaginary().getPtr(), N);
    CHECK(peak == k);
  }

  TEST_CASE("fft + inverseFft: round-trip scales by N") {
    // IFFT(FFT(x)) == N * x for the Mayer unnormalized transform.
    const unsigned N = 512;
    const float SR = 44100.0f;
    YSE::DSP::buffer orig(N);
    float* op = orig.getPtr();
    for (unsigned i = 0; i < N; ++i)
      op[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / SR);

    // Copy into re; imaginary = 0.
    YSE::DSP::buffer re(N), im(N);
    re = orig;
    im = 0.0f;

    YSE::DSP::fft fwd;
    fwd(re, im);

    // Copy FFT output before passing to inverseFft.
    YSE::DSP::buffer fwdRe(N), fwdIm(N);
    fwdRe = fwd.getReal();
    fwdIm = fwd.getImaginary();

    YSE::DSP::inverseFft inv;
    inv(fwdRe, fwdIm);

    float* rp = inv.getReal().getPtr();
    const float scale = static_cast<float>(N);
    for (unsigned i = 0; i < N; ++i)
      CHECK(rp[i] == doctest::Approx(scale * op[i]).epsilon(1e-3f));
  }

  TEST_CASE("fft: Parseval's theorem — freq energy equals N times time energy") {
    // For an unnormalized DFT: sum|X[k]|^2 = N * sum|x[n]|^2.
    const unsigned N = 512;
    const float SR = 44100.0f;
    YSE::DSP::buffer re(N);
    float* rp = re.getPtr();
    for (unsigned i = 0; i < N; ++i)
      rp[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / SR);

    float timeEnergy = 0.0f;
    for (unsigned i = 0; i < N; ++i)
      timeEnergy += rp[i] * rp[i];

    YSE::DSP::buffer im = TestHelpers::makeBuffer(N, 0.0f);
    YSE::DSP::fft f;
    f(re, im);

    float* fr = f.getReal().getPtr();
    float* fi = f.getImaginary().getPtr();
    float freqEnergy = 0.0f;
    for (unsigned i = 0; i < N; ++i)
      freqEnergy += fr[i] * fr[i] + fi[i] * fi[i];

    CHECK(freqEnergy == doctest::Approx(static_cast<float>(N) * timeEnergy).epsilon(0.01f));
  }

  // ─── realFft ─────────────────────────────────────────────────────────────────

  TEST_CASE("realFft: zero buffer stays zero") {
    YSE::DSP::realFft rf;
    YSE::DSP::buffer in = TestHelpers::makeBuffer(512, 0.0f);
    rf(in);
    float* rp = rf.getReal().getPtr();
    unsigned n = rf.getReal().getLength();
    for (unsigned i = 0; i < n; ++i)
      CHECK(std::abs(rp[i]) < 1e-6f);
  }

  TEST_CASE("realFft: peak bin matches input sine frequency") {
    const unsigned N = 512;
    const unsigned k = 8;
    const float SR = 44100.0f;
    const float freq = static_cast<float>(k) * SR / static_cast<float>(N);

    YSE::DSP::buffer in(N);
    float* ip = in.getPtr();
    for (unsigned i = 0; i < N; ++i)
      ip[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / SR);

    YSE::DSP::realFft rf;
    rf(in);

    unsigned peak = TestHelpers::peakBinIndex(rf.getReal().getPtr(), rf.getImaginary().getPtr(), N);
    CHECK(peak == k);
  }

  // ─── mayer low-level functions ────────────────────────────────────────────────

  TEST_CASE("mayer_fft: zero arrays remain zero after transform") {
    const int N = 256;
    std::vector<float> re(N, 0.0f);
    std::vector<float> im(N, 0.0f);
    mayer_fft(N, re.data(), im.data());
    for (int i = 0; i < N; ++i) {
      CHECK(std::abs(re[i]) < 1e-6f);
      CHECK(std::abs(im[i]) < 1e-6f);
    }
  }

  TEST_CASE("mayer_fft / mayer_ifft: round-trip scales by N") {
    // IFFT(FFT(x)) == N * x for the Mayer unnormalized pair.
    const int N = 256;
    std::vector<float> orig(N), re(N), im(N, 0.0f);
    for (int i = 0; i < N; ++i)
      orig[i] = re[i] = std::sin(2.0f * kPi * 3.0f * static_cast<float>(i) / static_cast<float>(N));

    mayer_fft(N, re.data(), im.data());
    mayer_ifft(N, re.data(), im.data());

    for (int i = 0; i < N; ++i)
      CHECK(re[i] == doctest::Approx(static_cast<float>(N) * orig[i]).epsilon(1e-3f));
  }

  TEST_CASE("mayer_realfft / mayer_realifft: round-trip scales by N") {
    const int N = 256;
    std::vector<float> orig(N), data(N);
    for (int i = 0; i < N; ++i)
      orig[i] = data[i] =
          std::sin(2.0f * kPi * 5.0f * static_cast<float>(i) / static_cast<float>(N));

    mayer_realfft(N, data.data());
    mayer_realifft(N, data.data());

    // mayer_realfft followed by mayer_realifft scales by N.
    for (int i = 0; i < N; ++i)
      CHECK(data[i] == doctest::Approx(static_cast<float>(N) * orig[i]).epsilon(1e-3f));
  }

} // TEST_SUITE("dsp")
