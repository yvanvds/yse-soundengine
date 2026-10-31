// Tests for YSE::DSP::delay — the variable-length circular delay line.
// No audio device required; SAMPLERATE is initialised to 48000 by the
// portaudioDeviceManager translation unit at static-initialisation time.
//
// Note: delay::read() accepts delayTime in integer milliseconds, which
// introduces quantisation (128 samples ≈ 2.9 ms at 44100 Hz).  Tests here
// focus on invariants that are exact with a 0 ms delay argument rather than
// attempting sample-precise round-trips with non-integer ms delays.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/delay.hpp"
#include "support/alloc_probe.hpp"
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

  TEST_CASE("delay: copy construction preserves the line's contents and state (issue #639)") {
    // The copy constructor used to execute `delay(source.size);` — an unnamed
    // temporary, not a delegating call — leaving the copy's buffer empty and
    // bufferlength/phase indeterminate. A copy must carry the full state.
    YSE::DSP::delay original(1000);
    YSE::DSP::buffer in(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      ptr[i] = static_cast<float>(i) * 0.01f;
    original.process(in);

    YSE::DSP::delay copy(original);

    // The copy holds the original's history.
    YSE::DSP::buffer result(128);
    copy.read(result, 0u);
    CHECK(TestHelpers::buffersNearlyEqual(result, in, 1e-5f));

    // And it keeps working as an independent line afterwards.
    YSE::DSP::buffer second(128);
    second = 0.5f;
    copy.process(second);
    copy.read(result, 0u);
    YSE::DSP::buffer expected(128);
    expected = 0.5f;
    CHECK(TestHelpers::buffersNearlyEqual(result, expected, 1e-5f));
  }

  TEST_CASE("delay: copy of a freshly constructed line is usable (issue #639)") {
    // Covers the never-processed state: phase/currentLength must be
    // initialised by construction, not only by the first process() resize.
    YSE::DSP::delay fresh(1000);
    YSE::DSP::delay copy(fresh);
    YSE::DSP::buffer in(128);
    in = 0.25f;
    copy.process(in);
    YSE::DSP::buffer result(128);
    copy.read(result, 0u);
    YSE::DSP::buffer expected(128);
    expected = 0.25f;
    CHECK(TestHelpers::buffersNearlyEqual(result, expected, 1e-5f));
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

  TEST_CASE("delay: process() and read() allocate nothing across a rate or size change (#1210)") {
    // process() used to buffer.resize() whenever the length derived from the
    // size and the live SAMPLERATE changed, and read() resized a result
    // shorter than the block — both on the audio thread.
    YSE::DSP::delay d(100);
    YSE::DSP::buffer in(128);
    in = 0.5f;
    YSE::DSP::buffer result(128);
    YSE::DSP::buffer shortResult(64);
    YSE::DSP::buffer ctrl(128);
    ctrl = 2.0f;
    d.process(in); // first block derives the length from the construction state

    {
      TestHelpers::ScopedSampleRate at96k(96000);
      TestHelpers::ProbeScope probe;
      d.process(in); // new rate: longer line
      d.read(result, 1u);
      d.read(result, ctrl);
      d.setSize(1000); // longer than the 100 ms the memory was sized for: clamped
      d.process(in);
      d.read(shortResult, 0u); // short target: no resize
      d.read(shortResult, ctrl);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(shortResult.getLength() == 64);

    // Still a working line after the clamp: a zero delay returns the last block.
    YSE::DSP::buffer second(128);
    second = 0.25f;
    d.process(second);
    d.read(result, 0u);
    YSE::DSP::buffer expected(128);
    expected = 0.25f;
    CHECK(TestHelpers::buffersNearlyEqual(result, expected, 1e-5f));
  }

  TEST_CASE("delay: a short result receives the start of the delayed block (#1210)") {
    YSE::DSP::delay d(100);
    YSE::DSP::buffer in(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      ptr[i] = static_cast<float>(i);
    d.process(in);
    YSE::DSP::buffer shortResult(64);
    d.read(shortResult, 0u);
    REQUIRE(shortResult.getLength() == 64);
    for (unsigned i = 0; i < 64; ++i)
      CHECK(shortResult.getPtr()[i] == doctest::Approx(static_cast<float>(i)));
  }

  TEST_CASE("delay: reserve() grows the line beyond its construction memory (#1210)") {
    // Sized for 10 ms at 8 kHz, then grown to 500 ms there: a 300 ms echo
    // (2400 samples) must surface instead of being clamped to ~10 ms.
    TestHelpers::ScopedSampleRate at8k(8000);
    YSE::DSP::delay d(10, 8000);
    d.reserve(500, 8000);
    YSE::DSP::buffer in(128);
    in = 0.f;
    in.getPtr()[0] = 1.f;
    d.process(in);
    in = 0.f;
    YSE::DSP::buffer result(128);
    int echoBlock = -1;
    for (int b = 1; b < 40 && echoBlock < 0; ++b) {
      d.process(in);
      d.read(result, 300u);
      if (TestHelpers::measureRms(result) > 1e-3f) echoBlock = b;
    }
    CHECK(echoBlock == 2400 / 128); // read offset 2400 + 128 lands in block 18
  }

  TEST_CASE("delay: a line longer than 2^32 / rate ms keeps its length (#1212)") {
    // The line length and the read offset were both a UInt product of the
    // rate and the time in ms, which wrapped past 2^32: a 46 s line at 96 kHz
    // came out ~1.3 s long and a 45 s read offset ~0.26 s.
    TestHelpers::ScopedSampleRate at96k(96000);
    YSE::DSP::delay d(46000, 96000);
    YSE::DSP::buffer in(1024);
    in = 0.f;
    in.getPtr()[0] = 1.f;
    d.process(in);
    in = 0.f;
    YSE::DSP::buffer result(1024);
    // 45 s = 4320000 samples; the read covers samples [b*1024 - 4320000, +1024)
    // relative to the impulse, which holds sample 0 for b = 4218.
    const int expectedBlock = 4218;
    int echoBlock = -1;
    for (int b = 1; b <= expectedBlock + 4 && echoBlock < 0; ++b) {
      d.process(in);
      d.read(result, 45000u);
      if (TestHelpers::measureRms(result) > 1e-3f) echoBlock = b;
    }
    CHECK(echoBlock == expectedBlock);
  }

} // TEST_SUITE("dsp")
