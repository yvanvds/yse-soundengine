// Tests for YSE::DSP::gainFader (YseEngine/dsp/dynamics/gainFader.hpp),
// issue #1168. Reference output is closed form: the gain of position p > 0 on
// a slider of `size` steps is scale * inc^(p - size), 0 at p = 0, and a move
// ramps the applied gain in a straight line from where it is to the new gain
// over round-down(time * rate / 1000) samples (at least 1), landing on it.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/dynamics/gainFader.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::gainFader;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  double Db(double ratio) {
    return 20.0 * std::log10(ratio);
  }

  // The closed form in double precision.
  double Expected(int p, int size, double scale, double inc) {
    return p <= 0 ? 0.0 : scale * std::pow(inc, p - size);
  }

  // The applied gain over `count` samples after a move from `from` to `to`
  // with a ramp of `length` samples, starting at the move.
  std::vector<double> Ramp(double from, double to, unsigned int length, unsigned int count) {
    std::vector<double> out;
    out.reserve(count);
    for (unsigned int n = 0; n < count; ++n)
      out.push_back(n + 1 >= length ? to : from + (to - from) * (n + 1) / length);
    return out;
  }

  // One block of ones through the fader: the gain it applied, per sample.
  buffer& Ones(gainFader& fader, UInt length = N) {
    static buffer in(N);
    in.resize(length);
    in = 1.f;
    return fader(in);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("gainFader: defaults (#1168)") {
    gainFader fader;
    CHECK(fader.position() == 0);
    CHECK(fader.size() == 158);
    CHECK(fader.scale() == 7.94321f);
    CHECK(fader.increment() == 1.071519f);
    CHECK(fader.time() == 10.f);
    CHECK(fader.gain() == 0.f);
    CHECK(fader.value() == 0.f);
  }

  TEST_CASE("gainFader: the taper is scale * inc ^ (p - size), 0 at position 0 (#1168)") {
    const auto gain = [](int p) {
      return static_cast<double>(gainFader::Gain(p, 158, 7.94321f, 1.071519f));
    };
    CHECK(gain(0) == 0.0);
    CHECK(gain(-5) == 0.0);
    for (int p = 1; p < 158; ++p) {
      CAPTURE(p);
      CHECK(gain(p) == doctest::Approx(Expected(p, 158, 7.94321, 1.071519)).epsilon(1e-6));
    }
    // The defaults' landmarks: 128 is unity, a step 0.6 dB and ten 6 dB, 1 is
    // 75.6 dB below 127, and 157 has 17.4 dB of headroom.
    CHECK(gain(128) == doctest::Approx(1.0).epsilon(1e-5));
    CHECK(Db(gain(129) / gain(128)) == doctest::Approx(0.6).epsilon(1e-3));
    CHECK(Db(gain(110) / gain(100)) == doctest::Approx(6.0).epsilon(1e-3));
    CHECK(Db(gain(127) / gain(1)) == doctest::Approx(75.6).epsilon(1e-3));
    CHECK(Db(gain(157) / gain(128)) == doctest::Approx(17.4).epsilon(1e-3));

    // Other settings follow the same formula.
    CHECK(gainFader::Gain(9, 10, 2.f, 2.f) == 1.f);
    CHECK(gainFader::Gain(3, 10, 2.f, 2.f) == doctest::Approx(Expected(3, 10, 2.0, 2.0)));
    CHECK(gainFader::Gain(5, 10, -1.f, 1.f) == -1.f);
  }

  TEST_CASE("gainFader: positions and sizes are whole and held to the slider (#1168)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(gainFader::Position(64.9f, 158) == 64);
    CHECK(gainFader::Position(0.5f, 158) == 0);
    CHECK(gainFader::Position(-3.f, 158) == 0);
    CHECK(gainFader::Position(157.f, 158) == 157);
    CHECK(gainFader::Position(158.f, 158) == 157);
    CHECK(gainFader::Position(1e30f, 158) == 157);
    CHECK(gainFader::Position(inf, 10) == 9);
    CHECK(gainFader::Position(nan, 158) == 0);
    CHECK(gainFader::Size(100.7f) == 100);
    CHECK(gainFader::Size(1.f) == 2);
    CHECK(gainFader::Size(-inf) == 2);
    CHECK(gainFader::Size(inf) == gainFader::MAX_SIZE);
    CHECK(gainFader::Size(nan) == gainFader::DEFAULT_SIZE);

    gainFader fader;
    fader.position(200.f);
    CHECK(fader.position() == 157);
    // A smaller size moves the position down to its top; a larger one does
    // not move it back.
    fader.size(100.f);
    CHECK(fader.size() == 100);
    CHECK(fader.position() == 99);
    fader.size(158.f);
    CHECK(fader.position() == 99);

    // Settings that are not numbers, and increments that are not above 0,
    // are ignored; a negative time is 0.
    fader.position(nan).size(nan).scale(nan).scale(inf).increment(0.f).increment(-2.f).increment(
        inf);
    fader.time(nan);
    CHECK(fader.position() == 99);
    CHECK(fader.size() == 158);
    CHECK(fader.scale() == 7.94321f);
    CHECK(fader.increment() == 1.071519f);
    CHECK(fader.time() == 10.f);
    fader.time(-5.f);
    CHECK(fader.time() == 0.f);
    fader.scale(-2.f).increment(2.f);
    CHECK(fader.gain() == doctest::Approx(-2.0 * std::pow(2.0, 99 - 158)));
  }

  TEST_CASE("gainFader: a move ramps the gain in a straight line over the time (#1168)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    gainFader fader;
    // 10 ms: ten samples from silence to unity, then held.
    fader.position(128.f);
    const double unity = Expected(128, 158, 7.94321, 1.071519);
    buffer& out = Ones(fader);
    const std::vector<double> ramp = Ramp(0.0, unity, 10, N);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(ramp[i]).epsilon(1e-6));
    }
    CHECK(out.getPtr()[9] == static_cast<float>(gainFader::Gain(128, 158, 7.94321f, 1.071519f)));
    CHECK(fader.value() == fader.gain());

    // The gain multiplies the signal: a sine comes out scaled. A move down
    // ramps down the same way.
    fader.time(20.f).position(118.f);
    buffer sine(N);
    for (UInt i = 0; i < N; ++i)
      sine.getPtr()[i] = static_cast<float>(std::sin(0.1 * i));
    buffer& scaled = fader(sine);
    const double down = Expected(118, 158, 7.94321, 1.071519);
    const std::vector<double> fall = Ramp(unity, down, 20, N);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(scaled.getPtr()[i] ==
            doctest::Approx(fall[i] * std::sin(0.1 * i)).epsilon(1e-5).scale(1e-6));
    }

    // A move during a ramp starts a new one from where the gain is.
    fader.time(10.f).position(128.f);
    Ones(fader, 4);
    const double at = Ramp(down, unity, 10, 4)[3];
    fader.position(0.f);
    buffer& back = Ones(fader);
    const std::vector<double> fade = Ramp(at, 0.0, 10, N);
    for (UInt i = 0; i < 12; ++i) {
      CAPTURE(i);
      CHECK(back.getPtr()[i] == doctest::Approx(fade[i]).epsilon(1e-6).scale(1e-6));
    }
    CHECK(back.getPtr()[9] == 0.f);

    // A time below one sample jumps.
    fader.time(0.5f).position(157.f);
    CHECK(Ones(fader).getPtr()[0] == fader.gain());
    fader.time(0.f).position(1.f);
    CHECK(Ones(fader).getPtr()[0] == fader.gain());
  }

  TEST_CASE("gainFader: the ramp follows the live sample rate (#1168)") {
    gainFader fader;
    fader.position(128.f);
    const float unity = fader.gain();
    {
      ScopedSampleRate rate(1000);
      buffer& out = Ones(fader);
      CHECK(out.getPtr()[8] < unity);
      CHECK(out.getPtr()[9] == unity);
    }
    {
      // At twice the rate, 10 ms is twice the samples.
      ScopedSampleRate rate(2000);
      fader.position(0.f);
      buffer& out = Ones(fader);
      CHECK(out.getPtr()[9] == doctest::Approx(unity / 2.0).epsilon(1e-5));
      CHECK(out.getPtr()[18] > 0.f);
      CHECK(out.getPtr()[19] == 0.f);
    }
  }

  TEST_CASE("gainFader: any block split gives the same output (#1168)") {
    ScopedSampleRate rate(1000);
    const UInt splits[] = {N, 64, 13, 1};
    // Moves at the start of blocks 0, 2 and 5 of N samples, with a ramp of
    // 300 samples that the next move interrupts.
    const int moves[] = {140, 60, 128};
    std::vector<float> reference;
    for (UInt split : splits) {
      CAPTURE(split);
      gainFader fader;
      fader.time(300.f);
      buffer in(N);
      std::vector<float> out;
      for (UInt block = 0; block < 8; ++block) {
        if (block == 0) fader.position(static_cast<float>(moves[0]));
        if (block == 2) fader.position(static_cast<float>(moves[1]));
        if (block == 5) fader.position(static_cast<float>(moves[2]));
        for (UInt start = 0; start < N; start += split) {
          const UInt length = std::min(split, N - start);
          in.resize(length);
          for (UInt i = 0; i < length; ++i)
            in.getPtr()[i] = static_cast<float>(std::cos(0.05 * (block * N + start + i)));
          buffer& y = fader(in);
          REQUIRE(y.getLength() == length);
          for (UInt i = 0; i < length; ++i)
            out.push_back(y.getPtr()[i]);
        }
      }
      if (reference.empty()) {
        reference = out;
        continue;
      }
      REQUIRE(out.size() == reference.size());
      for (std::size_t i = 0; i < out.size(); ++i) {
        CAPTURE(i);
        CHECK(out[i] == reference[i]);
      }
    }
    // And the reference is the closed form: a 300-sample ramp towards 140
    // cut off after 256 samples, then one from there that lands on 60.
    const double g140 = Expected(140, 158, 7.94321, 1.071519);
    const double g60 = Expected(60, 158, 7.94321, 1.071519);
    const double at255 = g140 * 256.0 / 300.0;
    const double at400 = at255 + (g60 - at255) * 145.0 / 300.0;
    const auto gainAt = [&](UInt n) { return reference[n] / std::cos(0.05 * n); };
    CHECK(gainAt(255) == doctest::Approx(at255).epsilon(1e-4));
    CHECK(gainAt(400) == doctest::Approx(at400).epsilon(1e-4));
    CHECK(gainAt(2 * N + 299) == doctest::Approx(g60).epsilon(1e-4));
  }

  TEST_CASE("gainFader: processing allocates nothing (#1168)") {
    ScopedSampleRate rate(1000);
    gainFader fader;
    buffer in(N);
    in = 0.5f;
    buffer shorter(13);
    shorter = 0.5f;
    fader(in);
    {
      TestHelpers::ProbeScope probe;
      fader.position(128.f).size(150.f).scale(2.f).increment(1.1f).time(5.f);
      fader(in);
      fader(shorter);
      fader(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(fader.value() == fader.gain());
  }

} // TEST_SUITE("dsp")
