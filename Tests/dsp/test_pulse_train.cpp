// Tests for YSE::DSP::pulseTrain (YseEngine/dsp/generators/pulseTrain.hpp),
// the class behind ~train (issue #1059). Most cases run at 1000 Hz so a
// millisecond is one sample, and use periods whose phase step is a power of
// two, so the phase lands on the cycle grid exactly.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/pulseTrain.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::pulseTrain;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Half = N / 2;

  struct Run {
    std::vector<float> out;
    std::vector<unsigned int> onsets; // absolute sample indices
  };

  // `total` samples from the set values, in blocks of `block`.
  Run Render(pulseTrain& fn, unsigned int total, unsigned int block = N) {
    Run r;
    for (unsigned int start = 0; start < total; start += block) {
      const unsigned int len = total - start < block ? total - start : block;
      buffer& b = fn(len);
      REQUIRE(b.getLength() == len);
      for (unsigned int i = 0; i < len; ++i)
        r.out.push_back(b.getPtr()[i]);
      for (unsigned int i = 0; i < fn.onsets(); ++i)
        r.onsets.push_back(start + fn.onset(i));
    }
    return r;
  }

  // The closed form for constant settings: sample n is on when the cycle
  // phase n / period, less the phase offset, wraps below the width.
  float Reference(unsigned int n, double period, double width, double phase) {
    double p = static_cast<double>(n) / period - phase;
    p -= std::floor(p);
    return p < width ? 1.f : 0.f;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("pulseTrain: defaults are a 1000 ms period at half width, high first (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    CHECK(fn.interval() == 1000.f);
    CHECK(fn.width() == 0.5f);
    CHECK(fn.phase() == 0.f);
    CHECK_FALSE(fn.deferInterval());
    const Run r = Render(fn, 2500);
    for (unsigned int n = 0; n < 2500; ++n)
      REQUIRE(r.out[n] == Reference(n, 1000.0, 0.5, 0.0));
    CHECK(r.out[0] == 1.f);
    CHECK(r.out[499] == 1.f);
    CHECK(r.out[500] == 0.f);
    REQUIRE(r.onsets.size() == 3);
    CHECK(r.onsets[0] == 0);
    CHECK(r.onsets[1] == 1000);
    CHECK(r.onsets[2] == 2000);
  }

  TEST_CASE("pulseTrain: width and phase against the closed form (#1059)") {
    ScopedSampleRate rate(1000);
    const double widths[] = {0.25, 0.5, 0.75, 0.125};
    const double phases[] = {0.0, 0.25, 0.5, 0.875};
    for (double w : widths) {
      for (double ph : phases) {
        CAPTURE(w);
        CAPTURE(ph);
        pulseTrain fn;
        fn.interval(16.f).width(static_cast<float>(w)).phase(static_cast<float>(ph));
        const Run r = Render(fn, 3 * N);
        for (unsigned int n = 0; n < 3 * N; ++n)
          REQUIRE(r.out[n] == Reference(n, 16.0, w, ph));
        // One onset per cycle, at the phase offset, and at sample 0 when the
        // first pulse wraps around the start of the cycle.
        const auto first = static_cast<unsigned int>(ph * 16.0);
        const bool wraps = ph + w > 1.0;
        std::vector<unsigned int> expected;
        if (wraps) expected.push_back(0);
        for (unsigned int n = first; n < 3 * N; n += 16)
          expected.push_back(n);
        REQUIRE(r.onsets == expected);
      }
    }
  }

  TEST_CASE("pulseTrain: width 0 is one sample on, width 1 one sample off (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.interval(8.f).width(0.f);
    Run r = Render(fn, 32);
    const float narrow[] = {1, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned int n = 0; n < 32; ++n)
      REQUIRE(r.out[n] == narrow[n % 8]);
    CHECK(r.onsets.size() == 4);

    fn.reset();
    fn.width(1.f);
    r = Render(fn, 32);
    const float wide[] = {1, 1, 1, 1, 1, 1, 1, 0};
    for (unsigned int n = 0; n < 32; ++n)
      REQUIRE(r.out[n] == wide[n % 8]);
    CHECK(r.onsets.size() == 4); // every period, after the off sample

    // A fractional period keeps exactly one sample on at width 0.
    fn.reset();
    fn.interval(7.3f).width(0.f);
    r = Render(fn, 7300);
    float sum = 0.f;
    for (float v : r.out)
      sum += v;
    CHECK(sum == doctest::Approx(1000.f).epsilon(0.002));
    CHECK(r.onsets.size() == static_cast<std::size_t>(sum));
  }

  TEST_CASE("pulseTrain: out-of-range settings are cleaned (#1059)") {
    pulseTrain fn;
    fn.width(2.f);
    CHECK(fn.width() == 1.f);
    fn.width(-1.f);
    CHECK(fn.width() == 0.f);
    fn.width(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.width() == 0.5f);
    fn.phase(1.25f);
    CHECK(fn.phase() == 0.25f);
    fn.phase(-0.25f);
    CHECK(fn.phase() == 0.75f);
    fn.phase(1.f);
    CHECK(fn.phase() == 0.f);
    fn.phase(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.phase() == 0.f);
    fn.interval(-5.f);
    CHECK(fn.interval() == 0.f);
    fn.interval(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.interval() == 0.f);
    fn.interval(12.5f);
    CHECK(fn.interval() == 12.5f);
  }

  TEST_CASE("pulseTrain: a fractional period does not drift (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.interval(7.3f);
    const Run r = Render(fn, 73000);
    // 73000 samples at 7.3 samples a period: 10000 periods, give or take
    // the float rounding of 7.3.
    CHECK(r.onsets.size() >= 9999);
    CHECK(r.onsets.size() <= 10001);
    // The gaps are 7 or 8 samples, never anything else.
    for (std::size_t k = 1; k < r.onsets.size(); ++k) {
      const unsigned int gap = r.onsets[k] - r.onsets[k - 1];
      REQUIRE((gap == 7 || gap == 8));
    }
  }

  TEST_CASE("pulseTrain: the output does not depend on the block size (#1059)") {
    ScopedSampleRate rate(44100);
    pulseTrain whole;
    whole.interval(3.7f).width(0.3f).phase(0.4f);
    const Run ref = Render(whole, 4 * N);
    const unsigned int blocks[] = {64, 32, 13, 1};
    for (unsigned int block : blocks) {
      CAPTURE(block);
      pulseTrain split;
      split.interval(3.7f).width(0.3f).phase(0.4f);
      const Run r = Render(split, 4 * N, block);
      REQUIRE(r.out == ref.out);
      REQUIRE(r.onsets == ref.onsets);
    }
  }

  TEST_CASE("pulseTrain: a new interval applies at once, or waits for the cycle in deferred "
            "mode (#1059)") {
    ScopedSampleRate rate(1000);
    // Two samples into a 4-sample cycle the phase is 0.5. At once, an
    // 8-sample period goes on from there: 4 samples to the next onset. Deferred,
    // the running cycle ends first.
    pulseTrain now;
    now.interval(4.f);
    Render(now, 2);
    now.interval(8.f);
    Run r = Render(now, 20);
    REQUIRE(r.onsets.size() >= 2);
    CHECK(r.onsets[0] == 4); // absolute sample 6
    CHECK(r.onsets[1] == 12);

    pulseTrain later;
    later.deferInterval(true);
    CHECK(later.deferInterval());
    later.interval(4.f);
    Render(later, 2);
    later.interval(8.f);
    r = Render(later, 20);
    REQUIRE(r.onsets.size() >= 2);
    CHECK(r.onsets[0] == 2); // absolute sample 4
    CHECK(r.onsets[1] == 10);
  }

  TEST_CASE("pulseTrain: a per-sample interval, immediate and deferred (#1059)") {
    ScopedSampleRate rate(1000);
    buffer ms(N);
    for (unsigned int i = 0; i < N; ++i)
      ms.getPtr()[i] = i < 66 ? 4.f : 8.f;

    pulseTrain now;
    now(ms);
    std::vector<unsigned int> onsets;
    onsets.reserve(N);
    for (unsigned int i = 0; i < now.onsets(); ++i)
      onsets.push_back(now.onset(i));
    // 4-sample cycles up to 64; at 66 the phase is 0.5 and the 8-sample
    // period takes it to 70.
    REQUIRE(onsets.size() == 17 + 8);
    CHECK(onsets[16] == 64);
    CHECK(onsets[17] == 70);
    CHECK(onsets[18] == 78);

    pulseTrain later;
    later.deferInterval(true);
    later(ms);
    onsets.clear();
    for (unsigned int i = 0; i < later.onsets(); ++i)
      onsets.push_back(later.onset(i));
    REQUIRE(onsets.size() >= 19);
    CHECK(onsets[16] == 64);
    CHECK(onsets[17] == 68); // the running 4-sample cycle ends first
    CHECK(onsets[18] == 76);
  }

  TEST_CASE("pulseTrain: per-sample width and phase (#1059)") {
    ScopedSampleRate rate(1000);
    buffer ms(N);
    ms = 8.f;
    buffer w(N);
    buffer ph(N);
    for (unsigned int i = 0; i < N; ++i) {
      w.getPtr()[i] = i < Half ? 0.25f : 0.75f;
      ph.getPtr()[i] = i < Half ? 0.f : 0.5f;
    }
    pulseTrain fn;
    buffer& out = fn(ms, w, ph);
    REQUIRE(out.getLength() == N);
    for (unsigned int n = 0; n < N; ++n) {
      const double width = n < Half ? 0.25 : 0.75;
      const double phase = n < Half ? 0.0 : 0.5;
      REQUIRE(out.getPtr()[n] == Reference(n, 8.0, width, phase));
    }
    // A shorter width buffer shortens the block.
    buffer shortW(16);
    shortW = 0.5f;
    CHECK(fn(ms, shortW, ph).getLength() == 16);
  }

  TEST_CASE("pulseTrain: trigger() starts a pulse at the next block (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.interval(16.f).phase(0.25f);
    Render(fn, 10); // the pulse runs from 4 to 11: the output is 1 here
    fn.trigger();
    Run r = Render(fn, 40);
    REQUIRE(r.onsets.size() >= 3);
    CHECK(r.onsets[0] == 0); // an onset although the output was already 1
    CHECK(r.onsets[1] == 16);
    CHECK(r.onsets[2] == 32);
    for (unsigned int n = 0; n < 40; ++n)
      REQUIRE(r.out[n] == Reference(n, 16.0, 0.5, 0.0));
  }

  TEST_CASE("pulseTrain: interval 0 stops; a stopped train adopts a new interval at once "
            "(#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.deferInterval(true);
    fn.interval(8.f);
    Render(fn, 2);
    fn.interval(0.f);
    Run r = Render(fn, 20); // the running cycle ends at 8 (6 from here), then holds
    REQUIRE(r.onsets.size() == 1);
    CHECK(r.onsets[0] == 6);
    for (unsigned int n = 6; n < 20; ++n)
      REQUIRE(r.out[n] == 1.f);
    fn.interval(4.f);
    r = Render(fn, 12);
    CHECK(r.out[0] == 1.f);
    CHECK(r.out[1] == 1.f);
    CHECK(r.out[2] == 0.f);
    REQUIRE(r.onsets.size() == 2);
    CHECK(r.onsets[0] == 4);
    CHECK(r.onsets[1] == 8);
  }

  TEST_CASE("pulseTrain: a period of one sample or less stays at 1 (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.interval(0.5f);
    Run r = Render(fn, 64);
    for (float v : r.out)
      REQUIRE(v == 1.f);
    CHECK(r.onsets.size() == 1);
  }

  TEST_CASE("pulseTrain: follows the live sample rate (#1059)") {
    pulseTrain fn;
    fn.interval(10.f);
    {
      ScopedSampleRate rate(1000);
      const Run r = Render(fn, 40);
      REQUIRE(r.onsets.size() == 4);
      CHECK(r.onsets[1] == 10);
    }
    fn.reset();
    {
      ScopedSampleRate rate(2000);
      const Run r = Render(fn, 40);
      REQUIRE(r.onsets.size() == 2);
      CHECK(r.onsets[1] == 20);
    }
  }

  TEST_CASE("pulseTrain: reset() starts over and keeps the settings (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    fn.interval(8.f).width(0.25f).phase(0.5f).deferInterval(true);
    Render(fn, 13);
    fn.trigger();
    fn.reset();
    CHECK(fn.onsets() == 0);
    CHECK(fn.interval() == 8.f);
    CHECK(fn.width() == 0.25f);
    CHECK(fn.phase() == 0.5f);
    CHECK(fn.deferInterval());
    const Run r = Render(fn, 32);
    for (unsigned int n = 0; n < 32; ++n)
      REQUIRE(r.out[n] == Reference(n, 8.0, 0.25, 0.5));
    CHECK(r.onsets.front() == 4); // the trigger was dropped
  }

  TEST_CASE("pulseTrain: a longer block is processed up to STANDARD_BUFFERSIZE (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    CHECK(fn(N * 2).getLength() == N);
    buffer ms(N * 2);
    ms = 4.f;
    CHECK(fn(ms).getLength() == N);
  }

  TEST_CASE("pulseTrain: processing allocates nothing (#1059)") {
    ScopedSampleRate rate(1000);
    pulseTrain fn;
    buffer ms(N);
    ms = 5.f;
    buffer w(N);
    w = 0.3f;
    buffer ph(N);
    ph = 0.1f;
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn();
      fn(ms);
      fn(ms, w, ph);
      fn(N / 2);
      fn.interval(3.f).width(0.7f).phase(0.2f).deferInterval(true);
      fn.trigger();
      fn();
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
