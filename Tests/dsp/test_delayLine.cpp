// Tests for YSE::DSP::delayLine and YSE::DSP::delayTap
// (YseEngine/dsp/delay/), issues #1038 and #1105. The reference input is the
// sample counter itself: sample n of the stream is n, so a tap at a delay of
// d samples must output n - d, and a linear input is reproduced exactly by
// the cubic interpolator too, at any fractional d.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/delay/delayLine.hpp"
#include "dsp/delay/delayTap.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  float Ms(double samples) {
    return static_cast<float>(samples * 1000.0 / static_cast<double>(YSE::SAMPLERATE));
  }

  // The next block of the counter stream.
  void Fill(YSE::DSP::buffer& block, double& n) {
    for (UInt i = 0; i < block.getLength(); ++i)
      block.getPtr()[i] = static_cast<float>(n++);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("delayLine: a fixed tap returns the input that many samples back (#1105)") {
    YSE::DSP::delayLine line(1000.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    const double delay = N + 37;
    double n = 0;
    for (int block = 0; block < 6; ++block) {
      const double first = n;
      line.beginBlock();
      Fill(in, n);
      line.write(in);
      YSE::DSP::buffer& out = tap(line, Ms(delay));
      for (UInt i = 0; i < N; ++i) {
        const double expected = first + i - delay;
        CHECK(out.getPtr()[i] == doctest::Approx(expected < 0 ? 0.0 : expected));
      }
    }
  }

  TEST_CASE("delayLine: a tap reads the same samples before or after the write (#1038)") {
    // This is what lets ~tapout skip waiting for ~tapin in the patcher: the
    // order the two run in does not change the output.
    YSE::DSP::delayLine before(500.f);
    YSE::DSP::delayLine after(500.f);
    YSE::DSP::delayTap tapBefore;
    YSE::DSP::delayTap tapAfter;
    YSE::DSP::buffer in(N);
    YSE::DSP::buffer delays(N);
    for (UInt i = 0; i < N; ++i)
      delays.getPtr()[i] = Ms(N + 3.25 + 0.5 * i);
    double n = 0;
    for (int block = 0; block < 5; ++block) {
      Fill(in, n);
      before.beginBlock();
      after.beginBlock();

      YSE::DSP::buffer& a = tapBefore(before, delays);
      before.write(in);
      after.write(in);
      YSE::DSP::buffer& b = tapAfter(after, delays);
      for (UInt i = 0; i < N; ++i)
        CHECK(a.getPtr()[i] == b.getPtr()[i]);
    }
  }

  TEST_CASE("delayLine: a signal delay interpolates between samples (#1105)") {
    YSE::DSP::delayLine line(1000.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    YSE::DSP::buffer delays(N);
    double n = 0;
    for (int block = 0; block < 4; ++block) {
      const double first = n;
      for (UInt i = 0; i < N; ++i)
        delays.getPtr()[i] = Ms(N + 10.25 + 0.125 * i); // a delay moving through fractions
      line.beginBlock();
      Fill(in, n);
      line.write(in);
      YSE::DSP::buffer& out = tap(line, delays);
      if (block < 2) continue; // the line is still filling
      for (UInt i = 0; i < N; ++i) {
        const double expected = first + i - (N + 10.25 + 0.125 * i);
        CHECK(out.getPtr()[i] == doctest::Approx(expected).epsilon(1e-4));
      }
    }
  }

  TEST_CASE("delayLine: delays are clamped to one block and to the maximum (#1105)") {
    YSE::DSP::delayLine line(50.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    double n = 0;
    for (int block = 0; block < 40; ++block) {
      line.beginBlock();
      Fill(in, n);
      line.write(in);
    }
    const double last = n - 1; // the newest sample in the line, at index N - 1 of this block
    const double longest = std::lround(0.05 * YSE::SAMPLERATE);

    SUBCASE("zero is one block") {
      YSE::DSP::buffer& out = tap(line, 0.f);
      CHECK(out.getPtr()[N - 1] == doctest::Approx(last - N));
    }
    SUBCASE("far past the maximum is the maximum") {
      YSE::DSP::buffer& out = tap(line, 10000.f);
      CHECK(out.getPtr()[N - 1] == doctest::Approx(last - longest));
    }
    SUBCASE("a signal delay of zero is one block and two samples") {
      YSE::DSP::buffer delays(N);
      delays = 0.f;
      YSE::DSP::buffer& out = tap(line, delays);
      CHECK(out.getPtr()[N - 1] == doctest::Approx(last - N - 2));
    }
  }

  TEST_CASE("delayLine: maximumDelay is held within the capacity and clears the line (#1105)") {
    YSE::DSP::delayLine line(20.f);
    CHECK(line.capacity() == doctest::Approx(20.f).epsilon(1e-3));
    line.maximumDelay(1000.f);
    CHECK(line.maximumDelay() == doctest::Approx(line.capacity()));
    line.maximumDelay(-5.f);
    CHECK(line.maximumDelay() == 0.f);

    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    in = 1.f;
    line.maximumDelay(10.f);
    line.beginBlock();
    line.write(in);
    line.beginBlock();
    line.write(in);
    CHECK(tap(line, 0.f).getPtr()[0] == 1.f);
    line.maximumDelay(10.f);
    CHECK(tap(line, 0.f).getPtr()[0] == 0.f);
  }

  TEST_CASE("delayLine: clear silences the line (#1105)") {
    YSE::DSP::delayLine line(100.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    in = 0.5f;
    for (int block = 0; block < 3; ++block) {
      line.beginBlock();
      line.write(in);
    }
    CHECK(tap(line, 0.f).getPtr()[0] == 0.5f);
    line.clear();
    CHECK(tap(line, 0.f).getPtr()[0] == 0.f);
  }

  TEST_CASE("delayLine: a frozen line loops its last maximumDelay of audio (#1105)") {
    YSE::DSP::delayLine line(1000.f);
    line.maximumDelay(Ms(3 * N)); // a period of three blocks
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    double n = 0;
    for (int block = 0; block < 8; ++block) {
      line.beginBlock();
      Fill(in, n);
      line.write(in);
    }
    line.freeze(true);
    CHECK(line.frozen());

    // Keep feeding new input: none of it may reach the taps.
    YSE::DSP::buffer seen[6];
    for (auto& b : seen) {
      line.beginBlock();
      Fill(in, n);
      line.write(in);
      b = tap(line, 0.f);
    }
    for (int k = 3; k < 6; ++k)
      for (UInt i = 0; i < N; ++i)
        CHECK(seen[k].getPtr()[i] == seen[k - 3].getPtr()[i]);
    CHECK(seen[0].getPtr()[0] < 8.0 * N); // recorded before the freeze

    line.freeze(false);
    for (int block = 0; block < 2; ++block) {
      line.beginBlock();
      Fill(in, n);
      line.write(in);
    }
    CHECK(tap(line, 0.f).getPtr()[N - 1] == doctest::Approx(n - 1 - N));
  }

  TEST_CASE("delayLine: the delay follows a sample-rate change (#1105)") {
    SampleRateGuard guard;
    YSE::DSP::delayLine line(100.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    for (UInt rate : {44100u, 96000u}) {
      YSE::SAMPLERATE = rate;
      line.clear();
      double n = 0;
      for (int block = 0; block < 30; ++block) {
        line.beginBlock();
        Fill(in, n);
        line.write(in);
      }
      // 20 ms is 882 samples at 44.1 kHz and 1920 at 96 kHz.
      const double samples = 0.02 * rate;
      CHECK(tap(line, 20.f).getPtr()[N - 1] == doctest::Approx(n - 1 - samples));
    }
  }

  TEST_CASE("delayLine: writing and tapping allocate nothing (#1105)") {
    YSE::DSP::delayLine line(200.f);
    YSE::DSP::delayTap tap;
    YSE::DSP::buffer in(N);
    YSE::DSP::buffer delays(N);
    in = 0.25f;
    delays = 12.5f;
    tap(line, 3.f);
    tap(line, delays);
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 4; ++block) {
        line.beginBlock();
        line.write(in);
        tap(line, 3.f);
        tap(line, delays);
      }
      line.freeze(true);
      line.write(in);
      line.clear();
      line.maximumDelay(50.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
