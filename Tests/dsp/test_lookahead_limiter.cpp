// Tests for YSE::DSP::lookaheadLimiter (YseEngine/dsp/dynamics/
// lookaheadLimiter.hpp), issue #1115. The reference is the algorithm written
// out directly in double: the needed gain per frame, its minimum over the
// last L + 1 frames, the release, and the average over the last L + 1
// frames, each by brute force rather than with the class's running queue
// and sum. A lone peak gives exact values by hand.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/dynamics/lookaheadLimiter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::lookaheadLimiter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Settings {
    unsigned int lookahead = lookaheadLimiter::DEFAULT_LOOKAHEAD;
    double thresholdDb = 0.0;
    double releaseMs = lookaheadLimiter::DEFAULT_RELEASE;
    double preampDb = 0.0;
    double postampDb = 0.0;
    lookaheadLimiter::MODE mode = lookaheadLimiter::EXPONENTIAL;
    double rate = 48000.0;
  };

  double Lin(double dB) {
    return std::pow(10.0, dB / 20.0);
  }

  // The algorithm in double, by brute force, without the safety clamp.
  // in[c][n] -> out[c][n]; also returns the gain per frame.
  std::vector<std::vector<double>> Reference(const std::vector<std::vector<float>>& in,
                                             const Settings& s,
                                             std::vector<double>* gains = nullptr) {
    const std::size_t channels = in.size();
    const std::size_t size = in[0].size();
    const auto L = static_cast<std::ptrdiff_t>(s.lookahead);
    const double thr = Lin(s.thresholdDb);
    const double pre = Lin(s.preampDb);
    const double post = Lin(s.postampDb);
    const double releaseSamples = s.releaseMs * 0.001 * s.rate;
    const double linearStep = releaseSamples < 1.0 ? 1.0 : 1.0 / releaseSamples;
    const double coef = releaseSamples < 1.0 ? 1.0 : 1.0 - std::exp(-1.0 / releaseSamples);

    std::vector<double> needed(size);
    std::vector<double> released(size);
    std::vector<std::vector<double>> out(channels, std::vector<double>(size, 0.0));
    double r = 1.0;
    for (std::size_t n = 0; n < size; ++n) {
      double level = 0.0;
      for (std::size_t c = 0; c < channels; ++c)
        level = std::max(level, std::fabs(static_cast<double>(in[c][n]) * pre));
      needed[n] = level > thr ? thr / level : 1.0;
      double target = 1.0;
      for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(n) - L;
           k <= static_cast<std::ptrdiff_t>(n); ++k)
        if (k >= 0) target = std::min(target, needed[static_cast<std::size_t>(k)]);
      if (target < r)
        r = target;
      else if (s.mode == lookaheadLimiter::LINEAR)
        r = std::min(target, r + linearStep);
      else
        r = r + (target - r) * coef;
      released[n] = r;
      double sum = 0.0;
      for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(n) - L;
           k <= static_cast<std::ptrdiff_t>(n); ++k)
        sum += k >= 0 ? released[static_cast<std::size_t>(k)] : 1.0;
      const double g = sum / static_cast<double>(L + 1);
      if (gains != nullptr) gains->push_back(g);
      const std::ptrdiff_t from = static_cast<std::ptrdiff_t>(n) - L;
      for (std::size_t c = 0; c < channels; ++c) {
        const double delayed =
            from >= 0 ? static_cast<double>(in[c][static_cast<std::size_t>(from)]) * pre : 0.0;
        out[c][n] = delayed * g * post;
      }
    }
    return out;
  }

  void Apply(lookaheadLimiter& f, const Settings& s) {
    f.lookahead(s.lookahead)
        .threshold(static_cast<Flt>(s.thresholdDb))
        .release(static_cast<Flt>(s.releaseMs))
        .preamp(static_cast<Flt>(s.preampDb))
        .postamp(static_cast<Flt>(s.postampDb))
        .mode(s.mode);
  }

  // Bursts of a loud tone with quiet gaps: limiting, release and recovery.
  float Burst(unsigned int n, unsigned int channel) {
    const auto x = static_cast<float>(n);
    const float envelope =
        (n / 700) % 3 == 0 ? 0.2f : 1.0f + 2.f * std::fabs(std::sin(x * 0.0013f));
    return envelope * std::sin(x * (0.05f + 0.013f * static_cast<float>(channel)));
  }

  std::vector<std::vector<float>> Input(std::size_t channels, std::size_t size) {
    std::vector<std::vector<float>> x(channels, std::vector<float>(size));
    for (std::size_t c = 0; c < channels; ++c)
      for (std::size_t n = 0; n < size; ++n)
        x[c][n] = Burst(static_cast<unsigned int>(n), static_cast<unsigned int>(c));
    return x;
  }

  // Runs `x` through `f` in blocks of N; returns the output per channel.
  std::vector<std::vector<float>> Run(lookaheadLimiter& f,
                                      const std::vector<std::vector<float>>& x) {
    const std::size_t channels = x.size();
    const std::size_t size = x[0].size();
    std::vector<std::vector<float>> y(channels, std::vector<float>(size));
    std::vector<buffer> blocks(channels, buffer(N));
    std::vector<buffer*> list(channels);
    for (std::size_t start = 0; start < size; start += N) {
      for (std::size_t c = 0; c < channels; ++c) {
        for (unsigned int i = 0; i < N; ++i)
          blocks[c].getPtr()[i] = x[c][start + i];
        list[c] = &blocks[c];
      }
      f(list.data(), static_cast<UInt>(channels));
      for (std::size_t c = 0; c < channels; ++c)
        for (unsigned int i = 0; i < N; ++i)
          y[c][start + i] = f.output(static_cast<UInt>(c)).getPtr()[i];
    }
    return y;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("lookaheadLimiter: defaults (#1115)") {
    lookaheadLimiter f;
    CHECK(f.channels() == 1);
    CHECK(f.maxLookahead() == lookaheadLimiter::DEFAULT_MAX_LOOKAHEAD);
    CHECK(f.maxLookahead() == 512);
    CHECK(f.lookahead() == lookaheadLimiter::DEFAULT_LOOKAHEAD);
    CHECK(f.lookahead() == 100);
    CHECK(f.latency() == 100);
    CHECK(f.threshold() == 0.f);
    CHECK(f.release() == 100.f);
    CHECK(f.preamp() == 0.f);
    CHECK(f.postamp() == 0.f);
    CHECK(f.mode() == lookaheadLimiter::EXPONENTIAL);
    CHECK_FALSE(f.dcBlock());
    CHECK_FALSE(f.bypass());
    CHECK(f.gain() == 1.f);
    CHECK(f.gainReduction() == 0.f);

    // Channel count and delay line are clamped; a short line limits the
    // lookahead.
    CHECK(lookaheadLimiter(0).channels() == 1);
    CHECK(lookaheadLimiter(1000).channels() == lookaheadLimiter::MAX_CHANNELS);
    CHECK(lookaheadLimiter(1, 1u << 20).maxLookahead() == lookaheadLimiter::MAX_LOOKAHEAD);
    lookaheadLimiter shortLine(2, 32);
    CHECK(shortLine.lookahead() == 32);
    shortLine.lookahead(1000);
    CHECK(shortLine.lookahead() == 32);
    CHECK(lookaheadLimiter(1, 0).lookahead() == 0);
  }

  TEST_CASE("lookaheadLimiter: below the threshold the output is the input, delayed bit for "
            "bit (#1115)") {
    for (const unsigned int L : {0u, 1u, 37u, 100u, 512u}) {
      CAPTURE(L);
      lookaheadLimiter f;
      f.lookahead(L);
      std::vector<float> x(N * 6);
      for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = 0.9f * std::sin(static_cast<float>(n) * 0.07f);
      const auto y = Run(f, {x});
      for (std::size_t n = 0; n < x.size(); ++n)
        CHECK(y[0][n] == (n >= L ? x[n - L] : 0.f));
      CHECK(f.gain() == 1.f);
      CHECK(f.gainReduction() == 0.f);
    }
  }

  TEST_CASE("lookaheadLimiter: a lone peak, by hand (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // Threshold 0 dB, lookahead 100: a sample of 2 at 200 needs gain 0.5. The
    // gain ramps from 1 at output 199 to 0.5 at output 300, when the peak
    // leaves the delay at exactly 1.
    lookaheadLimiter f;
    std::vector<float> x(N * 4, 0.f);
    x[200] = 2.f;
    std::vector<float> g;
    std::vector<float> y;
    for (const float sample : x) {
      y.push_back(f.process(sample));
      g.push_back(f.gain());
    }
    CHECK(g[199] == 1.f);
    for (unsigned int n = 200; n <= 300; ++n) {
      CAPTURE(n);
      // r is 0.5 from frame 200 to 300 (the peak is inside each window), so
      // the average over 101 frames loses 0.5 / 101 per frame.
      const double expected = (101.0 - 0.5 * static_cast<double>(n - 199)) / 101.0;
      CHECK(g[n] == doctest::Approx(expected).epsilon(1e-6));
    }
    CHECK(g[300] == 0.5f);
    CHECK(y[300] == 1.f);
    for (unsigned int n = 0; n < x.size(); ++n)
      if (n != 300) CHECK(y[n] == 0.f);
    // Then it recovers.
    CHECK(g[301] > 0.5f);
    CHECK(g.back() > g[400]);
    CHECK(g.back() < 1.f);
  }

  TEST_CASE("lookaheadLimiter: blocks against the reference, across settings (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<Settings> cases(6);
    cases[1].lookahead = 0;
    cases[2].lookahead = 13;
    cases[2].thresholdDb = -6.0;
    cases[2].releaseMs = 5.0;
    cases[3].mode = lookaheadLimiter::LINEAR;
    cases[3].releaseMs = 20.0;
    cases[3].thresholdDb = -3.0;
    cases[4].preampDb = 6.0;
    cases[4].postampDb = -2.0;
    cases[4].lookahead = 250;
    cases[5].releaseMs = 0.0;
    cases[5].lookahead = 64;
    for (std::size_t i = 0; i < cases.size(); ++i) {
      CAPTURE(i);
      const Settings& s = cases[i];
      for (const std::size_t channels : {1u, 2u, 3u}) {
        CAPTURE(channels);
        lookaheadLimiter f(static_cast<UInt>(channels));
        Apply(f, s);
        const auto x = Input(channels, N * 24);
        const auto y = Run(f, x);
        const auto expected = Reference(x, s);
        const double ceiling = Lin(s.thresholdDb) * Lin(s.postampDb);
        double largest = 0.0;
        for (std::size_t c = 0; c < channels; ++c) {
          for (std::size_t n = 0; n < y[c].size(); ++n) {
            CHECK(y[c][n] == doctest::Approx(expected[c][n]).epsilon(1e-4).scale(1.0));
            largest = std::max(largest, std::fabs(static_cast<double>(y[c][n])));
            // The reference, without the clamp, keeps to the ceiling too.
            CHECK(std::fabs(expected[c][n]) <= ceiling * (1.0 + 1e-9));
          }
        }
        // Brickwall: never above the threshold (times the postamp), and the
        // loud bursts do reach it.
        CHECK(largest <= ceiling * (1.0 + 1e-6));
        CHECK(largest > ceiling * 0.9);
      }
    }
  }

  TEST_CASE("lookaheadLimiter: the channels share one gain (#1115)") {
    // Channel 0 quiet, channel 1 loud: channel 0 is turned down with it.
    lookaheadLimiter f(2);
    std::vector<std::vector<float>> x(2, std::vector<float>(N * 4));
    for (std::size_t n = 0; n < x[0].size(); ++n) {
      x[0][n] = 0.25f * std::sin(static_cast<float>(n) * 0.03f);
      x[1][n] = 3.f * std::sin(static_cast<float>(n) * 0.011f);
    }
    const auto y = Run(f, x);
    const std::size_t L = f.lookahead();
    for (std::size_t n = L + 50; n < x[0].size(); ++n) {
      CAPTURE(n);
      if (std::fabs(x[0][n - L]) < 0.01f || std::fabs(x[1][n - L]) < 0.01f) continue;
      const double g0 = static_cast<double>(y[0][n]) / static_cast<double>(x[0][n - L]);
      const double g1 = static_cast<double>(y[1][n]) / static_cast<double>(x[1][n - L]);
      CHECK(g0 == doctest::Approx(g1).epsilon(1e-5));
      CHECK(g0 < 1.0);
    }
    CHECK(f.gainReduction() < -6.f);
  }

  TEST_CASE("lookaheadLimiter: preamp, postamp and the release curves (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // Preamp below the threshold: only gain and delay.
    {
      lookaheadLimiter f;
      f.lookahead(10).preamp(6.f).postamp(-20.f);
      std::vector<float> x(N, 0.f);
      for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = 0.25f * std::sin(static_cast<float>(n) * 0.1f);
      const auto y = Run(f, {x});
      for (std::size_t n = 10; n < x.size(); ++n)
        CHECK(y[0][n] == doctest::Approx(x[n - 10] * Lin(6.0) * Lin(-20.0)).epsilon(1e-5));
    }
    // After a step down, linear recovers by a straight line of 1 per release
    // time, exponential by a one-pole curve. Lookahead 0 so gain is r.
    for (const auto mode : {lookaheadLimiter::LINEAR, lookaheadLimiter::EXPONENTIAL}) {
      CAPTURE(static_cast<int>(mode));
      lookaheadLimiter f;
      f.lookahead(0).release(10.f).mode(mode);
      f.process(4.f); // needs 0.25
      CHECK(f.gain() == 0.25f);
      for (int i = 0; i < 120; ++i)
        f.process(0.f);
      // 10 ms at 48 kHz is 480 samples.
      if (mode == lookaheadLimiter::LINEAR)
        CHECK(f.gain() == doctest::Approx(0.25 + 120.0 / 480.0).epsilon(1e-4));
      else
        CHECK(f.gain() == doctest::Approx(1.0 - 0.75 * std::exp(-120.0 / 480.0)).epsilon(1e-4));
      for (int i = 0; i < 48000; ++i)
        f.process(0.f);
      // Exactly 1 in the end: the exponential curve does not stall short
      // of it in float.
      CHECK(f.gain() == 1.f);
      CHECK(f.gainReduction() == 0.f);
    }
  }

  TEST_CASE("lookaheadLimiter: dcblock, bypass, clear and a lookahead change (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // A DC offset is removed.
    {
      lookaheadLimiter f;
      f.dcBlock(true);
      CHECK(f.dcBlock());
      float y = 0.f;
      for (int i = 0; i < 48000; ++i)
        y = f.process(0.5f);
      CHECK(std::fabs(y) < 1e-4f);
    }
    // Bypass: the raw input delayed, however loud; the gain still follows.
    {
      lookaheadLimiter f;
      f.bypass(true).preamp(12.f).dcBlock(true);
      CHECK(f.bypass());
      std::vector<float> x(N * 3);
      for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = 3.f * std::sin(static_cast<float>(n) * 0.05f);
      const auto y = Run(f, {x});
      for (std::size_t n = 100; n < x.size(); ++n)
        CHECK(y[0][n] == x[n - 100]);
      CHECK(f.gain() < 0.5f);
      // Off again: limited at once.
      f.bypass(false);
      const auto z = Run(f, {x});
      for (const float v : z[0])
        CHECK(std::fabs(v) <= 1.f);
    }
    // clear() empties the delay line; a lookahead change clears too.
    {
      lookaheadLimiter f;
      for (int i = 0; i < 300; ++i)
        f.process(3.f);
      CHECK(f.gain() < 1.f);
      f.clear();
      CHECK(f.gain() == 1.f);
      CHECK(f.lookahead() == 100);
      for (int i = 0; i < 100; ++i)
        CHECK(f.process(0.f) == 0.f);
      for (int i = 0; i < 300; ++i)
        f.process(0.5f);
      f.lookahead(20);
      CHECK(f.latency() == 20);
      CHECK(f.gain() == 1.f);
      for (int i = 0; i < 20; ++i)
        CHECK(f.process(0.25f) == 0.f);
      CHECK(f.process(0.25f) == 0.25f);
      // The same value again does not clear.
      f.lookahead(20);
      CHECK(f.process(0.f) == 0.25f);
    }
  }

  TEST_CASE("lookaheadLimiter: non-finite settings are ignored, non-finite input counts as 0 "
            "(#1115)") {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    lookaheadLimiter f;
    f.threshold(-6.f).release(50.f).preamp(1.f).postamp(-1.f);
    f.threshold(nan).threshold(inf).release(nan).preamp(-inf).postamp(nan);
    CHECK(f.threshold() == -6.f);
    CHECK(f.release() == 50.f);
    CHECK(f.preamp() == 1.f);
    CHECK(f.postamp() == -1.f);
    f.release(-5.f);
    CHECK(f.release() == 0.f);
    f.release(1e9f);
    CHECK(f.release() == lookaheadLimiter::MAX_RELEASE);
    f.mode(static_cast<lookaheadLimiter::MODE>(7));
    CHECK(f.mode() == lookaheadLimiter::EXPONENTIAL);

    lookaheadLimiter g;
    g.lookahead(2);
    CHECK(g.process(nan) == 0.f);
    CHECK(g.process(inf) == 0.f);
    CHECK(g.process(-inf) == 0.f);
    CHECK(g.process(0.5f) == 0.f);
    CHECK(g.process(0.f) == 0.f);
    CHECK(g.process(0.f) == 0.5f);
    CHECK(g.gain() == 1.f);
  }

  TEST_CASE("lookaheadLimiter: per-sample, per-block and split-block calls agree (#1115)") {
    TestHelpers::ScopedSampleRate rate(44100);
    const auto x = Input(2, N * 8);
    lookaheadLimiter blocks(2);
    blocks.threshold(-4.f).release(30.f);
    const auto expected = Run(blocks, x);

    lookaheadLimiter frames(2);
    frames.threshold(-4.f).release(30.f);
    for (std::size_t n = 0; n < x[0].size(); ++n) {
      const float in[2] = {x[0][n], x[1][n]};
      float out[2] = {0.f, 0.f};
      frames.process(in, out);
      CHECK(out[0] == expected[0][n]);
      CHECK(out[1] == expected[1][n]);
    }

    for (const unsigned int split : {64u, 13u, 1u}) {
      CAPTURE(split);
      lookaheadLimiter f(2);
      f.threshold(-4.f).release(30.f);
      buffer a(split);
      buffer b(split);
      buffer* list[2] = {&a, &b};
      for (std::size_t start = 0; start + split <= x[0].size(); start += split) {
        for (unsigned int i = 0; i < split; ++i) {
          a.getPtr()[i] = x[0][start + i];
          b.getPtr()[i] = x[1][start + i];
        }
        f(list, 2);
        for (unsigned int i = 0; i < split; ++i) {
          CHECK(f.output(0).getPtr()[i] == expected[0][start + i]);
          CHECK(f.output(1).getPtr()[i] == expected[1][start + i]);
        }
      }
    }

    // Missing channels read silence; a mono call is channel 0.
    lookaheadLimiter mono(2);
    lookaheadLimiter monoRef(2);
    buffer in(N);
    buffer silent(N);
    in = 0.75f;
    silent = 0.f;
    buffer* both[2] = {&in, &silent};
    buffer& y = mono(in);
    monoRef(both, 2);
    CHECK(TestHelpers::buffersNearlyEqual(y, monoRef.output(0), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(mono.output(1), monoRef.output(1), 0.f));
    CHECK(&mono.output(9) == &mono.output(1));
  }

  TEST_CASE("lookaheadLimiter: the release follows the live sample rate (#1115)") {
    for (const UInt sr : {44100u, 48000u, 96000u}) {
      CAPTURE(sr);
      TestHelpers::ScopedSampleRate scoped(sr);
      Settings s;
      s.rate = static_cast<double>(sr);
      s.releaseMs = 15.0;
      lookaheadLimiter f;
      Apply(f, s);
      const auto x = Input(1, N * 8);
      const auto y = Run(f, x);
      const auto expected = Reference(x, s);
      for (std::size_t n = 0; n < y[0].size(); ++n)
        CHECK(y[0][n] == doctest::Approx(expected[0][n]).epsilon(1e-4).scale(1.0));
    }
    // A rate change after construction is picked up: 10 ms is twice as many
    // samples at 96 kHz.
    lookaheadLimiter f;
    f.lookahead(0).release(10.f).mode(lookaheadLimiter::LINEAR);
    {
      TestHelpers::ScopedSampleRate first(48000);
      f.process(0.f);
    }
    TestHelpers::ScopedSampleRate second(96000);
    f.process(4.f);
    for (int i = 0; i < 240; ++i)
      f.process(0.f);
    CHECK(f.gain() == doctest::Approx(0.25 + 240.0 / 960.0).epsilon(1e-4));
  }

  TEST_CASE("lookaheadLimiter: a longer block is processed up to STANDARD_BUFFERSIZE (#1115)") {
    lookaheadLimiter f;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(f(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(f(longIn).getLength() == N);
    buffer* none[1] = {nullptr};
    CHECK(f(none, 0).getLength() == 0);
  }

  TEST_CASE("lookaheadLimiter: processing allocates nothing (#1115)") {
    lookaheadLimiter f(2, 1024);
    buffer in(N);
    buffer shortIn(32);
    in = 3.f;
    shortIn = 0.3f;
    buffer* list[2] = {&in, &in};
    f(list, 2);
    {
      TestHelpers::ProbeScope probe;
      f(list, 2);
      f(shortIn);
      f(in);
      f.process(0.5f);
      const float frameIn[2] = {1.f, 2.f};
      float frameOut[2];
      f.process(frameIn, frameOut);
      f.threshold(-3.f).release(10.f).preamp(2.f).postamp(-1.f);
      f.mode(lookaheadLimiter::LINEAR).dcBlock(true).bypass(true).bypass(false);
      f.lookahead(700).clear();
      f(list, 2);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
