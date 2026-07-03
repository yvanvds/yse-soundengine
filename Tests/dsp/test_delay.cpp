// Tests for YSE::DSP::delay — the variable-length circular delay line.
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.
//
// Note: delay::read() accepts delayTime in integer milliseconds, which
// introduces quantisation (128 samples ≈ 2.9 ms at 44100 Hz).  Tests here
// focus on invariants that are exact with a 0 ms delay argument rather than
// attempting sample-precise round-trips with non-integer ms delays.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/delay.hpp"
#include "support/audio_helpers.hpp"

TEST_SUITE("dsp") {

  TEST_CASE("delay: zero delay time returns the most recently written buffer") {
    // After process(in), read(result, 0) should recover the exact same samples.
    // Derivation: delaySamples = SAMPLERATE*0*0.001 + currentLength = currentLength,
    // so ph = phase - currentLength = write start position. ✓
    YSE::DSP::delay d(1000); // 1000 ms internal buffer
    YSE::DSP::buffer in(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      ptr[i] = static_cast<float>(i) * 0.01f;
    d.process(in);
    YSE::DSP::buffer result(128);
    d.read(result, 0u);
    CHECK(TestHelpers::buffersNearlyEqual(result, in, 1e-5f));
  }

  TEST_CASE("delay: second write replaces first at zero delay") {
    YSE::DSP::delay d(1000);
    YSE::DSP::buffer first(128);
    first = 1.0f;
    YSE::DSP::buffer second(128);
    second = 0.5f;
    d.process(first);
    d.process(second);
    YSE::DSP::buffer result(128);
    d.read(result, 0u);
    YSE::DSP::buffer expected(128);
    expected = 0.5f;
    CHECK(TestHelpers::buffersNearlyEqual(result, expected, 1e-5f));
  }

  TEST_CASE("delay: large delay on sparsely written buffer returns zeros") {
    // After writing one buffer, a 500 ms read-back wraps to an area never written.
    YSE::DSP::delay d(1000);
    YSE::DSP::buffer in(128);
    in = 1.0f;
    d.process(in);
    YSE::DSP::buffer result(128);
    d.read(result, 500u);
    float* ptr = result.getPtr();
    for (unsigned i = 0; i < result.getLength(); ++i)
      CHECK(std::abs(ptr[i]) < 1e-5f);
  }

  TEST_CASE("delay: setSize updates delay capacity without crash") {
    YSE::DSP::delay d(100);
    d.setSize(500);
    YSE::DSP::buffer in(128);
    in = 0.75f;
    d.process(in);
    YSE::DSP::buffer result(128);
    d.read(result, 0u);
    YSE::DSP::buffer expected(128);
    expected = 0.75f;
    CHECK(TestHelpers::buffersNearlyEqual(result, expected, 1e-5f));
  }

} // TEST_SUITE("dsp")
