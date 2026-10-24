// Tests for YSE::DSP::phasorSwing (YseEngine/dsp/generators/phasorSwing.hpp),
// issue #1076. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128 and the output for
// swing s is the closed form x / s below s and (x - s) / (1 - s) from there.

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorSwing.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorSwing;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int PERIOD = 128;

  // `samples` samples of a 375 Hz DSP::phasor (128 samples per cycle).
  std::vector<float> Ramp(unsigned int samples) {
    TestHelpers::ScopedSampleRate rate(48000);
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(375.f, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  // The closed form for input x (0..1) and swing s.
  double Swung(double x, double s) {
    return x < s ? x / s : (x - s) / (1.0 - s);
  }

  // The closed form at sample n of the 128-sample input.
  double Expected(unsigned int n, double s) {
    return Swung(static_cast<double>(n % PERIOD) / PERIOD, s);
  }

  // Sample by sample through process(), calling `before(n)` ahead of
  // sample n so a test can change the class at an exact sample.
  std::vector<float> Process(phasorSwing& fn, const std::vector<float>& in,
                             const std::function<void(unsigned int)>& before = nullptr) {
    std::vector<float> out;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (before) before(n);
      out.push_back(fn.process(in[n]));
    }
    return out;
  }

  // The samples where the output starts a new ramp (falls), after sample 0.
  std::vector<unsigned int> Starts(const std::vector<float>& out) {
    std::vector<unsigned int> starts;
    for (unsigned int n = 1; n < out.size(); ++n)
      if (out[n] < out[n - 1]) starts.push_back(n);
    return starts;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorSwing: defaults and setters (#1076)") {
    phasorSwing fn;
    CHECK(fn.swing() == 0.5f);
    CHECK_FALSE(fn.syncUpdate());
    fn.swing(0.75f).syncUpdate(true);
    CHECK(fn.swing() == 0.75f);
    CHECK(fn.syncUpdate());
    // Clamped to 0.15 .. 0.85 as in Max.
    fn.swing(0.1f);
    CHECK(fn.swing() == phasorSwing::MIN_SWING);
    fn.swing(-3.f);
    CHECK(fn.swing() == 0.15f);
    fn.swing(0.9f);
    CHECK(fn.swing() == phasorSwing::MAX_SWING);
    fn.swing(12.f);
    CHECK(fn.swing() == 0.85f);
    // Non-finite values are ignored.
    fn.swing(0.3f);
    fn.swing(std::numeric_limits<float>::quiet_NaN());
    fn.swing(std::numeric_limits<float>::infinity());
    fn.swing(-std::numeric_limits<float>::infinity());
    CHECK(fn.swing() == 0.3f);
  }

  TEST_CASE("phasorSwing: two ramps per input cycle against the closed form (#1076)") {
    const std::vector<float> in = Ramp(4 * PERIOD);
    for (double s : {0.15, 0.25, 0.5, 0.6, 0.75, 0.85}) {
      CAPTURE(s);
      phasorSwing fn;
      fn.swing(static_cast<float>(s));
      const std::vector<float> out = Process(fn, in);
      bool close = true;
      for (unsigned int n = 0; n < out.size(); ++n) {
        const double want = Expected(n, static_cast<float>(s));
        if (std::fabs(out[n] - want) > 1e-5) {
          CAPTURE(n);
          close = false;
        }
        CHECK(out[n] >= 0.f);
        CHECK(out[n] < 1.f);
      }
      CHECK(close);

      // Two ramps per cycle: the second starts at the first sample at or past
      // s, the next first ramp at the input wrap.
      const auto first = static_cast<unsigned int>(std::ceil(static_cast<float>(s) * PERIOD));
      std::vector<unsigned int> want;
      for (unsigned int c = 0; c < 4; ++c) {
        if (c > 0) want.push_back(c * PERIOD);
        want.push_back(c * PERIOD + first);
      }
      CHECK(Starts(out) == want);
    }

    // 0.75 exactly: the first ramp 96 samples, the second 32.
    phasorSwing fn;
    fn.swing(0.75f);
    const std::vector<float> out = Process(fn, in);
    CHECK(out[0] == 0.f);
    CHECK(out[48] == doctest::Approx(0.5));
    CHECK(out[95] == doctest::Approx(95.0 / 96.0));
    CHECK(out[96] == doctest::Approx(0.0));
    CHECK(out[112] == doctest::Approx(0.5));
    CHECK(out[127] == doctest::Approx(31.0 / 32.0));
    CHECK(out[128] == 0.f);

    // 0.5 is the same as a ramp at double speed.
    phasorSwing even;
    const std::vector<float> doubled = Process(even, in);
    for (unsigned int n = 0; n < doubled.size(); ++n)
      CHECK(doubled[n] == doctest::Approx(static_cast<double>(n % 64) / 64.0));
  }

  TEST_CASE("phasorSwing: syncupdate off lands a new swing on the next sample (#1076)") {
    const std::vector<float> in = Ramp(3 * PERIOD);
    phasorSwing fn;
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 40) fn.swing(0.25f); // mid first ramp: jumps into the second
      if (n == 150) fn.swing(0.75f);
    });
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const double s = n < 40 ? 0.5 : (n < 150 ? 0.25 : 0.75);
      CHECK(out[n] == doctest::Approx(Expected(n, s)));
    }
    CHECK(out[40] == doctest::Approx(Swung(40.0 / 128.0, 0.25)));
  }

  TEST_CASE("phasorSwing: syncupdate on lands a new swing at the input wrap (#1076)") {
    const std::vector<float> in = Ramp(4 * PERIOD);

    // Set before the first sample: applies from it.
    {
      phasorSwing fn;
      fn.syncUpdate(true).swing(0.75f);
      const std::vector<float> out = Process(fn, in);
      for (unsigned int n = 0; n < PERIOD; ++n)
        CHECK(out[n] == doctest::Approx(Expected(n, 0.75)));
    }

    // Set during the first ramp, then again right at the output's mid-cycle
    // reset: neither lands before the input wraps, so no ramp changes length.
    phasorSwing fn;
    fn.syncUpdate(true);
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 20) fn.swing(0.25f);
      if (n == 64) fn.swing(0.75f);
      if (n == 128 + 100) fn.swing(0.6f);
      if (n == 3 * 128 + 10) fn.syncUpdate(false); // lands the waiting swing at once
      if (n == 3 * 128 + 5) fn.swing(0.3f);
    });
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      double s = 0.5;
      if (n >= 128) s = 0.75; // the last swing set when the input wrapped
      if (n >= 256) s = 0.6;
      if (n >= 3 * 128 + 10) s = 0.3;
      CHECK(out[n] == doctest::Approx(Expected(n, s)));
    }
    std::vector<unsigned int> want = {64, 128, 128 + 96, 256};
    const auto starts = Starts(out);
    REQUIRE(starts.size() >= 4);
    CHECK(std::vector<unsigned int>(starts.begin(), starts.begin() + 4) == want);
  }

  TEST_CASE("phasorSwing: falling, out-of-range and non-finite input (#1076)") {
    const std::vector<float> rising = Ramp(3 * PERIOD);

    // A falling ramp gives two falling ramps; a waiting swing lands where it
    // wraps backwards (0 -> 1).
    std::vector<float> falling;
    falling.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      falling.push_back(n % PERIOD == 0 ? 0.f : 1.f - rising[n]);
    phasorSwing down;
    down.syncUpdate(true);
    const std::vector<float> outDown = Process(down, falling, [&](unsigned int n) {
      if (n == 60) down.swing(0.75f);
    });
    for (unsigned int n = 0; n < outDown.size(); ++n) {
      CAPTURE(n);
      const double x = n % PERIOD == 0 ? 0.0 : 1.0 - static_cast<double>(n % PERIOD) / PERIOD;
      CHECK(outDown[n] == doctest::Approx(Swung(x, n < 129 ? 0.5 : 0.75)));
    }

    // Out of range: wrapped into 0..1.
    std::vector<float> shifted;
    shifted.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      shifted.push_back(rising[n] + static_cast<float>(n % 3) * 2.f - 2.f);
    phasorSwing wrapped;
    wrapped.swing(0.6f);
    const std::vector<float> outWrapped = Process(wrapped, shifted);
    for (unsigned int n = 0; n < outWrapped.size(); ++n) {
      CAPTURE(n);
      CHECK(outWrapped[n] == doctest::Approx(Expected(n, 0.6)).epsilon(1e-4));
    }

    // Not a finite number: the previous input (0 before any).
    std::vector<float> holes(rising);
    holes[0] = std::numeric_limits<float>::quiet_NaN();
    holes[70] = std::numeric_limits<float>::infinity();
    holes[71] = -std::numeric_limits<float>::infinity();
    holes[72] = std::numeric_limits<float>::quiet_NaN();
    phasorSwing holed;
    const std::vector<float> out = Process(holed, holes);
    CHECK(out[0] == 0.f);
    CHECK(out[70] == out[69]);
    CHECK(out[72] == out[69]);
    CHECK(out[73] == doctest::Approx(Expected(73, 0.5)));
    for (float y : out)
      CHECK(std::isfinite(y));
  }

  TEST_CASE("phasorSwing: blocks match samples; nothing depends on the sample rate (#1076)") {
    const std::vector<float> in = Ramp(2 * N);
    phasorSwing samples;
    samples.swing(0.7f);
    const std::vector<float> one = Process(samples, in);

    phasorSwing blocks;
    blocks.swing(0.7f);
    buffer chunk(N);
    std::vector<float> two;
    for (unsigned int b = 0; b < 2; ++b) {
      for (unsigned int i = 0; i < N; ++i)
        chunk.getPtr()[i] = in[b * N + i];
      buffer& out = blocks(chunk);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i)
        two.push_back(out.getPtr()[i]);
    }
    CHECK(one == two);

    TestHelpers::ScopedSampleRate rate(48000);
    phasorSwing a;
    a.swing(0.3f);
    const std::vector<float> at48 = Process(a, in);
    YSE::SAMPLERATE = 96000;
    phasorSwing b;
    b.swing(0.3f);
    const bool same = Process(b, in) == at48;
    CHECK(same);
  }

  TEST_CASE("phasorSwing: processing and setters allocate nothing (#1076)") {
    phasorSwing fn;
    buffer in(N);
    in = 0.25f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn.swing(0.7f).syncUpdate(true);
    fn(in);
    (void)fn.process(0.5f);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
