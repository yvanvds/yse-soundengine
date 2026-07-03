// Tests for YSE::DSP oscillator classes, wavetable, interpolate4, and Normalize.
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/oscillators.hpp"
#include "dsp/wavetable.hpp"
#include "dsp/interpolate4.hpp"
#include "dsp/sample_functions.hpp"
#include "support/audio_helpers.hpp"

TEST_SUITE("dsp") {

  // ─── sine ────────────────────────────────────────────────────────────────────

  TEST_CASE("sine: all samples bounded in [-1, +1]") {
    YSE::DSP::sine osc;
    YSE::DSP::buffer& buf = osc(440.0f, 128);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= -1.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  // Pick a frequency that divides SAMPLERATE exactly so 100 samples cover
  // exactly one period. At 44100 Hz this is 441 Hz; at 48000 Hz it's 480 Hz.
  // The choice keeps the test rate-agnostic while still hitting integer samples
  // per period (so the periodicity / zero-mean checks don't drift on FP rounding).
  static inline float oneHundredSamplePeriodFreq() {
    return (float)YSE::SAMPLERATE / 100.0f;
  }

  TEST_CASE("sine: output is periodic with period = 100 samples") {
    YSE::DSP::sine osc;
    osc.reset();
    YSE::DSP::buffer& buf = osc(oneHundredSamplePeriodFreq(), 400);
    CHECK(TestHelpers::checkPeriodicity(buf, 100, 0.01f));
  }

  TEST_CASE("sine: zero mean over a full period (100 samples)") {
    YSE::DSP::sine osc;
    osc.reset();
    YSE::DSP::buffer& buf = osc(oneHundredSamplePeriodFreq(), 100);
    float* ptr = buf.getPtr();
    float sum = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i)
      sum += ptr[i];
    CHECK(std::abs(sum) < 1.0f);
  }

  TEST_CASE("sine: buffer-frequency overload produces bounded output") {
    YSE::DSP::sine osc;
    YSE::DSP::buffer freq(128);
    freq = 440.0f;
    YSE::DSP::buffer& buf = osc(freq);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= -1.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  // ─── saw ─────────────────────────────────────────────────────────────────────

  TEST_CASE("saw: first sample is zero when phase starts at zero") {
    YSE::DSP::saw osc;
    YSE::DSP::buffer& buf = osc(440.0f, 128);
    CHECK(buf.getPtr()[0] == 0.0f);
  }

  TEST_CASE("saw: all samples bounded in [0, 1)") {
    YSE::DSP::saw osc;
    // Run four consecutive buffer-fills to cover wrapping behaviour.
    for (int call = 0; call < 4; ++call) {
      YSE::DSP::buffer& buf = osc(440.0f, 128);
      float* ptr = buf.getPtr();
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
    }
  }

  // ─── noise ───────────────────────────────────────────────────────────────────

  TEST_CASE("noise: all samples bounded in [-1, +1]") {
    YSE::DSP::noise osc;
    YSE::DSP::buffer& buf = osc(256);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= -1.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  TEST_CASE("noise: output is not constant across 128 samples") {
    YSE::DSP::noise osc;
    YSE::DSP::buffer& buf = osc(128);
    float* ptr = buf.getPtr();
    bool allSame = true;
    for (unsigned i = 1; i < buf.getLength(); ++i) {
      if (ptr[i] != ptr[0]) {
        allSame = false;
        break;
      }
    }
    CHECK(!allSame);
  }

  // ─── wavetable + oscillator ──────────────────────────────────────────────────

  TEST_CASE("wavetable: createSaw fundamental has sine shape") {
    // createSaw(1, 512) with internal phase=-0.25*2pi produces a sine wave:
    // zero at index 0, peak at 128, zero at 256, trough at 384.
    YSE::DSP::wavetable wt;
    wt.createSaw(1, 512);
    float* ptr = wt.getPtr();
    CHECK(ptr[0] == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(ptr[128] == doctest::Approx(1.0f).epsilon(0.01f));
    CHECK(ptr[256] == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(ptr[384] == doctest::Approx(-1.0f).epsilon(0.01f));
  }

  TEST_CASE("oscillator: saw wavetable produces bounded output") {
    YSE::DSP::wavetable wt;
    wt.createSaw(8, 512);
    YSE::DSP::oscillator osc;
    osc.initialize(wt);
    YSE::DSP::buffer& buf = osc(440.0f, 128);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= -1.1f);
      CHECK(ptr[i] <= 1.1f);
    }
  }

  TEST_CASE("oscillator: two instances with same initial phase produce identical output") {
    YSE::DSP::wavetable wt;
    wt.createSaw(8, 512);
    YSE::DSP::oscillator osc1, osc2;
    osc1.initialize(wt);
    osc2.initialize(wt);
    YSE::DSP::buffer& buf1 = osc1(440.0f, 64);
    YSE::DSP::buffer& buf2 = osc2(440.0f, 64);
    float* p1 = buf1.getPtr();
    float* p2 = buf2.getPtr();
    for (unsigned i = 0; i < 64; ++i)
      CHECK(p1[i] == doctest::Approx(p2[i]).epsilon(1e-6f));
  }

  // ─── interpolate4 ────────────────────────────────────────────────────────────

  TEST_CASE("interpolate4: integer offset on linear source returns exact sample") {
    // Source: 0,1,2,3,4,5,6,7. At index 2 (frac=0): a=1, b=2, c=3, d=4 → 2.0.
    YSE::DSP::buffer src(8);
    float* sp = src.getPtr();
    for (unsigned i = 0; i < 8; ++i)
      sp[i] = static_cast<float>(i);

    YSE::DSP::buffer in(1);
    in.getPtr()[0] = 2.0f;

    YSE::DSP::interpolate4 interp;
    interp.source(src);
    YSE::DSP::buffer& out = interp(in);
    CHECK(out.getPtr()[0] == doctest::Approx(2.0f).epsilon(1e-5f));
  }

  TEST_CASE("interpolate4: fractional offset on linear source gives exact linear result") {
    // For a linear ramp, 4-point cubic reduces to linear: index 2.5 → 2.5.
    YSE::DSP::buffer src(8);
    float* sp = src.getPtr();
    for (unsigned i = 0; i < 8; ++i)
      sp[i] = static_cast<float>(i);

    YSE::DSP::buffer in(1);
    in.getPtr()[0] = 2.5f;

    YSE::DSP::interpolate4 interp;
    interp.source(src);
    YSE::DSP::buffer& out = interp(in);
    CHECK(out.getPtr()[0] == doctest::Approx(2.5f).epsilon(1e-5f));
  }

  TEST_CASE("interpolate4: no source set returns zero") {
    YSE::DSP::buffer in(1);
    in.getPtr()[0] = 2.0f;
    YSE::DSP::interpolate4 interp;
    YSE::DSP::buffer& out = interp(in);
    CHECK(out.getPtr()[0] == 0.0f);
  }

  // ─── sample_functions ────────────────────────────────────────────────────────

  TEST_CASE("Normalize: scales max positive value to 1.0") {
    YSE::DSP::buffer buf(4);
    float* ptr = buf.getPtr();
    ptr[0] = 0.25f;
    ptr[1] = 0.50f;
    ptr[2] = 0.75f;
    ptr[3] = -0.10f;
    YSE::DSP::Normalize(buf);
    CHECK(ptr[2] == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(ptr[0] == doctest::Approx(0.25f / 0.75f).epsilon(1e-5f));
  }

  TEST_CASE("Normalize: all-negative buffer is unchanged") {
    // getMaxAmplitude returns 0 for all-negative input; Normalize is a no-op.
    YSE::DSP::buffer buf(3);
    float* ptr = buf.getPtr();
    ptr[0] = -0.3f;
    ptr[1] = -0.7f;
    ptr[2] = -0.5f;
    YSE::DSP::Normalize(buf);
    CHECK(ptr[0] == doctest::Approx(-0.3f).epsilon(1e-5f));
    CHECK(ptr[1] == doctest::Approx(-0.7f).epsilon(1e-5f));
  }

} // TEST_SUITE("dsp")
