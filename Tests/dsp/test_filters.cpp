// Tests for YSE::DSP filter classes: highPass, lowPass, bandPass, biQuad,
// realOnePole, realOneZero.
// No audio device required; SAMPLERATE is initialised to 48000 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/filters.hpp"
#include "dsp/rawFilters.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

TEST_SUITE("dsp") {

  // ─── highPass ─────────────────────────────────────────────────────────────────

  TEST_CASE("highPass: setFrequency(0) produces coef=1 passthrough") {
    YSE::DSP::highPass hp;
    hp.setFrequency(0.0f); // coef = 1 - 0 = 1.0 → operator() returns `in` unchanged
    YSE::DSP::buffer in(128);
    float* p = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      p[i] = static_cast<float>(i) * 0.01f;
    YSE::DSP::buffer& out = hp(in);
    CHECK(&out == &in);
  }

  TEST_CASE("highPass: DC input attenuated to near zero after settling") {
    // coef1 = 1 - 500*2π/44100 ≈ 0.929; after 128 samples: 0.929^127 ≈ 8e-5.
    YSE::DSP::highPass hp;
    hp.setFrequency(500.0f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    YSE::DSP::buffer* last = nullptr;
    for (int i = 0; i < 5; ++i)
      last = &hp(dc);
    CHECK(std::abs(last->getPtr()[127]) < 0.001f);
  }

  TEST_CASE("highPass: output stays bounded for DC input") {
    YSE::DSP::highPass hp;
    hp.setFrequency(500.0f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    for (int iter = 0; iter < 5; ++iter) {
      YSE::DSP::buffer& out = hp(dc);
      float* ptr = out.getPtr();
      for (unsigned i = 0; i < out.getLength(); ++i) {
        CHECK(ptr[i] >= -2.0f);
        CHECK(ptr[i] <= 2.0f);
      }
    }
  }

  // ─── lowPass ──────────────────────────────────────────────────────────────────

  TEST_CASE("lowPass: DC input converges to input level after settling") {
    // coef1 = 500*2π/44100 ≈ 0.071; time constant ≈ 14 samples.
    YSE::DSP::lowPass lp;
    lp.setFrequency(500.0f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    YSE::DSP::buffer* last = nullptr;
    for (int i = 0; i < 5; ++i)
      last = &lp(dc);
    CHECK(last->getPtr()[127] == doctest::Approx(1.0f).epsilon(0.001f));
  }

  TEST_CASE("lowPass: output monotonically increases toward DC on first buffer") {
    YSE::DSP::lowPass lp;
    lp.setFrequency(500.0f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    YSE::DSP::buffer& out = lp(dc);
    float* ptr = out.getPtr();
    CHECK(ptr[0] < ptr[127]);
    CHECK(ptr[0] >= 0.0f);
    CHECK(ptr[127] <= 1.0f);
  }

  TEST_CASE("lowPass: output stays in [0, 1] for positive DC input") {
    YSE::DSP::lowPass lp;
    lp.setFrequency(500.0f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    for (int iter = 0; iter < 5; ++iter) {
      YSE::DSP::buffer& out = lp(dc);
      float* ptr = out.getPtr();
      for (unsigned i = 0; i < out.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
    }
  }

  // ─── bandPass ─────────────────────────────────────────────────────────────────

  TEST_CASE("bandPass: output stays bounded for on-frequency sinusoidal input") {
    YSE::DSP::bandPass bp;
    bp.set(1000.0f, 2.0f);
    YSE::DSP::buffer in(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      ptr[i] = std::sin(2.0f * 3.14159f * 1000.0f * static_cast<float>(i) / 44100.0f);
    for (int iter = 0; iter < 10; ++iter) {
      YSE::DSP::buffer& out = bp(in);
      float* op = out.getPtr();
      for (unsigned i = 0; i < out.getLength(); ++i)
        CHECK(std::abs(op[i]) < 10.0f);
    }
  }

  TEST_CASE("bandPass: non-zero RMS output for on-frequency sinusoidal input") {
    YSE::DSP::bandPass bp;
    bp.set(1000.0f, 2.0f);
    YSE::DSP::buffer in(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      ptr[i] = std::sin(2.0f * 3.14159f * 1000.0f * static_cast<float>(i) / 44100.0f);
    for (int w = 0; w < 10; ++w)
      bp(in); // warm up to steady state
    YSE::DSP::buffer& out = bp(in);
    CHECK(TestHelpers::measureRms(out) > 0.0f);
  }

  // ─── biQuad ───────────────────────────────────────────────────────────────────

  TEST_CASE("biQuad lowpass: DC signal reaches unity gain after settling") {
    YSE::DSP::biQuad bq;
    bq.set(YSE::BQ_LOWPASS, 1000.0f, 0.707f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    YSE::DSP::buffer* last = nullptr;
    for (int i = 0; i < 20; ++i)
      last = &bq(dc);
    CHECK(last->getPtr()[127] == doctest::Approx(1.0f).epsilon(0.02f));
  }

  TEST_CASE("biQuad highpass: DC signal is blocked") {
    // At DC (z=1), highpass transfer function has zero numerator sum (ff1+ff2+ff3=0).
    YSE::DSP::biQuad bq;
    bq.set(YSE::BQ_HIGHPASS, 1000.0f, 0.707f);
    YSE::DSP::buffer dc(128);
    dc = 1.0f;
    YSE::DSP::buffer* last = nullptr;
    for (int i = 0; i < 20; ++i)
      last = &bq(dc);
    CHECK(std::abs(last->getPtr()[127]) < 0.01f);
  }

  // ─── rawFilters ───────────────────────────────────────────────────────────────

  TEST_CASE("realOnePole: impulse response decays exponentially with coef=0.9") {
    // Formula: out[n] = coef^n for an impulse at sample 0.
    YSE::DSP::realOnePole pole;
    YSE::DSP::buffer in1(128);
    in1 = 0.0f;
    in1.getPtr()[0] = 1.0f;
    YSE::DSP::buffer in2(128);
    in2 = 0.9f;
    YSE::DSP::buffer& out = pole(in1, in2);
    float* ptr = out.getPtr();
    CHECK(ptr[0] == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(ptr[1] == doctest::Approx(0.9f).epsilon(1e-5f));
    CHECK(ptr[2] == doctest::Approx(0.81f).epsilon(1e-4f));
    CHECK(ptr[4] == doctest::Approx(std::pow(0.9f, 4)).epsilon(1e-4f));
    CHECK(std::abs(ptr[10]) < std::abs(ptr[0]));
  }

  TEST_CASE("realOneZero: FIR difference filter response") {
    // out[n] = in[n] - coef*in[n-1]; with coef=0.5 and impulse at [0]:
    // out[0]=1, out[1]=-0.5, out[2]=0 (FIR: no further memory).
    YSE::DSP::realOneZero zero;
    YSE::DSP::buffer in1(128);
    in1 = 0.0f;
    in1.getPtr()[0] = 1.0f;
    YSE::DSP::buffer in2(128);
    in2 = 0.5f;
    YSE::DSP::buffer& out = zero(in1, in2);
    float* ptr = out.getPtr();
    CHECK(ptr[0] == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(ptr[1] == doctest::Approx(-0.5f).epsilon(1e-5f));
    CHECK(ptr[2] == doctest::Approx(0.0f).epsilon(1e-5f));
  }

  // ─── rawFilters: block lengths other than STANDARD_BUFFERSIZE (issue #651) ────
  //
  // The output buffers were fixed at STANDARD_BUFFERSIZE by construction while
  // the render loops wrote in1.getLength() samples. A longer input therefore
  // wrote past the end of the allocation (heap corruption, no diagnostic), and
  // a shorter one returned a buffer whose length did not describe its valid
  // samples. Both failure modes are visible below without a sanitizer: the
  // length assertions fail on the unpatched code, and the value assertions read
  // the region the overrun used to write into.

  TEST_CASE("rawFilters: real filters honour the input length, not STANDARD_BUFFERSIZE") {
    // Deliberately both below and above STANDARD_BUFFERSIZE (128): the short
    // case used to under-report the valid range, the long case overran.
    unsigned n = 0;
    SUBCASE("shorter than STANDARD_BUFFERSIZE") {
      n = 64;
    }
    SUBCASE("longer than STANDARD_BUFFERSIZE") {
      n = 256;
    }
    REQUIRE(n != YSE::STANDARD_BUFFERSIZE);

    YSE::DSP::buffer in1 = TestHelpers::makeImpulse(n);
    YSE::DSP::buffer in2(n);

    {
      YSE::DSP::realOnePole pole;
      in2 = 0.9f;
      YSE::DSP::buffer& out = pole(in1, in2);
      CHECK(out.getLength() == n);
      CHECK(out.getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out.getPtr()[1] == doctest::Approx(0.9f).epsilon(1e-5f));
      // The last sample is the one an overrun would have written out of bounds.
      CHECK(out.getPtr()[n - 1] ==
            doctest::Approx(std::pow(0.9f, static_cast<float>(n - 1))).epsilon(1e-4f));
    }

    {
      YSE::DSP::realOneZero zero;
      in2 = 0.5f;
      YSE::DSP::buffer& out = zero(in1, in2);
      CHECK(out.getLength() == n);
      CHECK(out.getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out.getPtr()[1] == doctest::Approx(-0.5f).epsilon(1e-5f));
      CHECK(out.getPtr()[n - 1] == doctest::Approx(0.0f).epsilon(1e-5f));
    }

    {
      // out[i] = in1[i-1] - in2[i]*in1[i]: the impulse shows up at [0] as
      // -coef*1 and at [1] as the delayed 1.
      YSE::DSP::realOneZeroReversed rzero;
      in2 = 0.5f;
      YSE::DSP::buffer& out = rzero(in1, in2);
      CHECK(out.getLength() == n);
      CHECK(out.getPtr()[0] == doctest::Approx(-0.5f).epsilon(1e-5f));
      CHECK(out.getPtr()[1] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out.getPtr()[n - 1] == doctest::Approx(0.0f).epsilon(1e-5f));
    }
  }

  TEST_CASE("rawFilters: complex filters honour the input length, not STANDARD_BUFFERSIZE") {
    unsigned n = 0;
    SUBCASE("shorter than STANDARD_BUFFERSIZE") {
      n = 64;
    }
    SUBCASE("longer than STANDARD_BUFFERSIZE") {
      n = 256;
    }
    REQUIRE(n != YSE::STANDARD_BUFFERSIZE);

    // Channel 0 is the real part, channel 1 the imaginary part.
    MULTICHANNELBUFFER in1(2);
    MULTICHANNELBUFFER in2(2);
    for (int ch = 0; ch < 2; ++ch) {
      in1[ch].resize(n);
      in2[ch].resize(n);
    }
    in1[0] = TestHelpers::makeImpulse(n);
    in1[1] = 0.0f;
    in2[1] = 0.0f; // purely real coefficient keeps the expected values simple

    {
      YSE::DSP::complexOnePole pole;
      in2[0] = 0.9f;
      MULTICHANNELBUFFER& out = pole(in1, in2);
      REQUIRE(out.size() == 2);
      CHECK(out[0].getLength() == n);
      CHECK(out[1].getLength() == n);
      CHECK(out[0].getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out[0].getPtr()[1] == doctest::Approx(0.9f).epsilon(1e-5f));
      CHECK(out[0].getPtr()[n - 1] ==
            doctest::Approx(std::pow(0.9f, static_cast<float>(n - 1))).epsilon(1e-4f));
    }

    {
      YSE::DSP::complexOneZero zero;
      in2[0] = 0.5f;
      MULTICHANNELBUFFER& out = zero(in1, in2);
      REQUIRE(out.size() == 2);
      CHECK(out[0].getLength() == n);
      CHECK(out[1].getLength() == n);
      CHECK(out[0].getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out[0].getPtr()[1] == doctest::Approx(-0.5f).epsilon(1e-5f));
      CHECK(out[0].getPtr()[n - 1] == doctest::Approx(0.0f).epsilon(1e-5f));
    }

    {
      YSE::DSP::complexOneZeroReversed rzero;
      in2[0] = 0.5f;
      MULTICHANNELBUFFER& out = rzero(in1, in2);
      REQUIRE(out.size() == 2);
      CHECK(out[0].getLength() == n);
      CHECK(out[1].getLength() == n);
      CHECK(out[0].getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
      CHECK(out[0].getPtr()[n - 1] == doctest::Approx(0.0f).epsilon(1e-5f));
    }
  }

  // The remaining length precondition (in1 and in2 must agree) is not covered
  // by a case here: it is enforced by a live `if` that runs in every build —
  // see rawFilters.cpp — but the `assert` beside it deliberately aborts a debug
  // build, and every test preset in CMakePresets.json is a Debug build, so a
  // case exercising it could only ever be dead `#ifdef NDEBUG` code.

  TEST_CASE("rawFilters: steady-state processing at a fixed length does not allocate") {
    // The resize is a grow-only cost in practice — std::vector keeps capacity —
    // so after the first block at a given length the audio path must be
    // allocation-free (project RT rule; issue #651 "Done when").
    if (!TestHelpers::probeCountsAllocations()) return;

    YSE::DSP::realOnePole pole;
    YSE::DSP::realOneZero zero;
    YSE::DSP::buffer in1(64);
    YSE::DSP::buffer in2(64);
    in1 = 0.25f;
    in2 = 0.5f;

    // First block sizes the outputs; everything after it is steady state.
    pole(in1, in2);
    zero(in1, in2);

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 32; ++i) {
        pole(in1, in2);
        zero(in1, in2);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
