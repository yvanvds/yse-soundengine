// Tests for YSE::DSP::adsr (YseEngine/dsp/generators/adsr.hpp), the class
// behind ~adsr (issue #1064). Most cases run at 1000 Hz so a millisecond is
// one sample: a segment of N ms reaches its target on its Nth sample.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/adsr.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::adsr;
using YSE::DSP::buffer;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

  // A straight segment from `a` to `b` over `n` samples, at its sample k
  // (0-based): it reaches `b` on sample n - 1.
  float Line(float a, float b, unsigned int n, unsigned int k) {
    return a + (b - a) * static_cast<float>(k + 1) / static_cast<float>(n);
  }

  std::vector<float> Copy(buffer& b) {
    return std::vector<float>(b.getPtr(), b.getPtr() + b.getLength());
  }

  // A gate signal of N samples: `value` from `on` up to (not including) `off`.
  buffer Gate(unsigned int on, unsigned int off, float value) {
    buffer g(N);
    g = 0.f;
    for (unsigned int i = on; i < off && i < N; ++i)
      g.getPtr()[i] = value;
    return g;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("adsr: defaults make a gate at the amplitude (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    CHECK(fn.attack() == 0.f);
    CHECK(fn.decay() == 0.f);
    CHECK(fn.sustain() == 1.f);
    CHECK(fn.release() == 0.f);
    CHECK(fn.retrigger() == 5.f);
    CHECK(fn.maxSustain() == -1.f);
    CHECK_FALSE(fn.legato());
    CHECK_FALSE(fn.playToEnd());
    CHECK(fn.stage() == adsr::IDLE);

    buffer& rest = fn();
    REQUIRE(rest.getLength() == N);
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rest.getPtr()[i] == 0.f);
    CHECK_FALSE(fn.started());
    CHECK_FALSE(fn.active());

    fn.trigger(0.7f);
    const std::vector<float> on = Copy(fn());
    CHECK(fn.started());
    CHECK(fn.active());
    CHECK(fn.stage() == adsr::SUSTAIN);
    for (unsigned int i = 0; i < N; ++i) {
      REQUIRE(on[i] == doctest::Approx(0.7f));
      REQUIRE(fn.gate().getPtr()[i] == 1.f);
    }

    fn.trigger(0.f);
    buffer& off = fn();
    CHECK(fn.finished());
    CHECK_FALSE(fn.started());
    CHECK_FALSE(fn.active());
    for (unsigned int i = 0; i < N; ++i) {
      REQUIRE(off.getPtr()[i] == 0.f);
      REQUIRE(fn.gate().getPtr()[i] == 0.f);
    }
  }

  TEST_CASE("adsr: a triggered envelope follows the closed form (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(4.f).decay(4.f).sustain(0.5f).release(8.f);
    fn.trigger(0.8f);
    const std::vector<float> a = Copy(fn());
    const std::vector<float> ga = Copy(fn.gate());
    for (unsigned int k = 0; k < N; ++k) {
      CAPTURE(k);
      float want = 0.4f;
      if (k < 4)
        want = Line(0.f, 0.8f, 4, k);
      else if (k < 8)
        want = Line(0.8f, 0.4f, 4, k - 4);
      REQUIRE(a[k] == doctest::Approx(want));
      REQUIRE(ga[k] == 1.f);
    }
    CHECK(fn.stage() == adsr::SUSTAIN);
    CHECK_FALSE(fn.finished());

    fn.trigger(0.f);
    const std::vector<float> r = Copy(fn());
    for (unsigned int k = 0; k < N; ++k) {
      CAPTURE(k);
      REQUIRE(r[k] == doctest::Approx(k < 8 ? Line(0.4f, 0.f, 8, k) : 0.f));
      REQUIRE(fn.gate().getPtr()[k] == 0.f);
    }
    CHECK(r[7] == 0.f);
    CHECK(fn.finished());
    CHECK(fn.stage() == adsr::IDLE);
  }

  TEST_CASE("adsr: a gate signal starts at its value and releases on 0; only a rise from 0 "
            "retriggers (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(4.f).decay(0.f).sustain(0.5f).release(4.f);
    buffer g = Gate(10, 60, 0.6f);
    for (unsigned int i = 30; i < 40; ++i)
      g.getPtr()[i] = 1.f; // non-zero to non-zero: ignored
    const std::vector<float> out = Copy(fn(g));
    const std::vector<float> open = Copy(fn.gate());
    CHECK(fn.started());
    CHECK(fn.finished());
    for (unsigned int k = 0; k < N; ++k) {
      CAPTURE(k);
      float want = 0.f;
      if (k >= 10 && k < 14)
        want = Line(0.f, 0.6f, 4, k - 10);
      else if (k >= 14 && k < 60)
        want = 0.3f; // a decay of 0 lands at the sustain on its first sample
      else if (k >= 60 && k < 64)
        want = Line(0.3f, 0.f, 4, k - 60);
      REQUIRE(out[k] == doctest::Approx(want));
      REQUIRE(open[k] == ((k >= 10 && k < 60) ? 1.f : 0.f));
    }

    // A gate that stays high across blocks does not retrigger.
    adsr held;
    held.attack(4.f);
    buffer high(N);
    high = 1.f;
    held(high);
    CHECK(held.started());
    held(high);
    CHECK_FALSE(held.started());
    CHECK(held.stage() == adsr::SUSTAIN);

    // A gate sample that is not a number counts as 0.
    high.getPtr()[5] = NaN;
    held(high);
    CHECK(held.stage() == adsr::SUSTAIN); // released at 5, restarted at 6
    CHECK(held.level() == doctest::Approx(1.f));
  }

  TEST_CASE("adsr: a restart ramps down over the retrigger time, then attacks from 0 (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(4.f).retrigger(4.f);
    fn.trigger(1.f);
    fn();
    fn.trigger(0.5f);
    const std::vector<float> out = Copy(fn());
    CHECK_FALSE(fn.started()); // not from rest
    for (unsigned int k = 0; k < 8; ++k) {
      CAPTURE(k);
      REQUIRE(out[k] == doctest::Approx(k < 4 ? Line(1.f, 0.f, 4, k) : Line(0.f, 0.5f, 4, k - 4)));
      REQUIRE(fn.gate().getPtr()[k] == (k < 4 ? 0.f : 1.f));
    }
    CHECK(out[N - 1] == doctest::Approx(0.5f));

    // A retrigger time of 0 attacks from 0 at once.
    fn.retrigger(0.f);
    fn.trigger(1.f);
    const std::vector<float> now = Copy(fn());
    for (unsigned int k = 0; k < 4; ++k)
      REQUIRE(now[k] == doctest::Approx(Line(0.f, 1.f, 4, k)));
  }

  TEST_CASE("adsr: legato restarts from the current level without a ramp down (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(4.f).sustain(0.5f).legato(true);
    fn.trigger(1.f);
    fn();
    CHECK(fn.level() == doctest::Approx(0.5f));
    fn.trigger(1.f);
    const std::vector<float> out = Copy(fn());
    for (unsigned int k = 0; k < 4; ++k) {
      REQUIRE(out[k] == doctest::Approx(Line(0.5f, 1.f, 4, k)));
      REQUIRE(fn.gate().getPtr()[k] == 1.f);
    }
    CHECK(out[4] == doctest::Approx(0.5f)); // a decay of 0
  }

  TEST_CASE("adsr: a release starts from the current level, also mid-attack (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(8.f).release(4.f);
    fn.trigger(1.f);
    fn(4); // half the attack
    CHECK(fn.level() == doctest::Approx(0.5f));
    fn.trigger(0.f);
    const std::vector<float> out = Copy(fn(8));
    for (unsigned int k = 0; k < 8; ++k)
      REQUIRE(out[k] == doctest::Approx(k < 4 ? Line(0.5f, 0.f, 4, k) : 0.f));
    CHECK(fn.finished());
  }

  TEST_CASE("adsr: maxSustain limits the sustain; 0 makes an ADR envelope (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.release(4.f).maxSustain(10.f);
    fn.trigger(1.f);
    const std::vector<float> out = Copy(fn());
    // Attack at 0, decay at 1, ten sustain samples 2-11, release from 12.
    for (unsigned int k = 0; k < 20; ++k) {
      CAPTURE(k);
      const float want = k < 12 ? 1.f : (k < 16 ? Line(1.f, 0.f, 4, k - 12) : 0.f);
      REQUIRE(out[k] == doctest::Approx(want));
    }
    CHECK(fn.started());
    CHECK(fn.finished());

    adsr adr;
    adr.attack(2.f).decay(2.f).sustain(0.5f).release(2.f).maxSustain(0.f);
    adr.trigger(1.f);
    const std::vector<float> r = Copy(adr());
    const float want[] = {0.5f, 1.f, 0.75f, 0.5f, 0.25f, 0.f, 0.f};
    for (unsigned int k = 0; k < 7; ++k)
      REQUIRE(r[k] == doctest::Approx(want[k]));
  }

  TEST_CASE("adsr: play to end runs the whole envelope and ignores triggers meanwhile "
            "(#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(2.f).decay(2.f).sustain(0.5f).release(2.f).playToEnd(true);
    fn.trigger(1.f);
    std::vector<float> out = Copy(fn(2));
    fn.trigger(0.f); // ignored
    for (float v : Copy(fn(2)))
      out.push_back(v);
    fn.trigger(0.3f); // ignored
    for (float v : Copy(fn(4)))
      out.push_back(v);
    CHECK(fn.finished());
    const float want[] = {0.5f, 1.f, 0.75f, 0.5f, 0.25f, 0.f, 0.f, 0.f};
    for (unsigned int k = 0; k < 8; ++k)
      REQUIRE(out[k] == doctest::Approx(want[k]));

    // A short gate plays it all too, with maxSustain as the sustain.
    fn.maxSustain(3.f);
    buffer g = Gate(0, 1, 1.f);
    const std::vector<float> gated = Copy(fn(g));
    const float held[] = {0.5f, 1.f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f, 0.25f, 0.f};
    for (unsigned int k = 0; k < 9; ++k)
      REQUIRE(gated[k] == doctest::Approx(held[k]));
  }

  TEST_CASE("adsr: per-sample times and sustain (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    buffer a(N);
    a = 4.f;
    buffer d(N);
    d = 0.f;
    buffer s(N);
    s = 0.5f;
    for (unsigned int i = 20; i < N; ++i)
      s.getPtr()[i] = 0.25f;
    buffer r(N);
    r = 2.f;
    fn.trigger(1.f);
    const std::vector<float> out = Copy(fn(a, d, s, r));
    for (unsigned int k = 0; k < 4; ++k)
      REQUIRE(out[k] == doctest::Approx(Line(0.f, 1.f, 4, k)));
    CHECK(out[10] == doctest::Approx(0.5f));
    CHECK(out[20] == doctest::Approx(0.25f)); // the held level follows the signal

    // With a gate too: release at 2 ms from the signal.
    buffer g = Gate(0, 10, 1.f);
    adsr gated;
    const std::vector<float> both = Copy(gated(g, a, d, s, r));
    CHECK(both[3] == doctest::Approx(1.f));
    CHECK(both[9] == doctest::Approx(0.5f));
    CHECK(both[10] == doctest::Approx(0.25f));
    CHECK(both[11] == 0.f);
    CHECK(gated.finished());
  }

  TEST_CASE("adsr: a time changed mid-segment keeps the fraction covered (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(8.f);
    fn.trigger(1.f);
    fn(4);
    CHECK(fn.level() == doctest::Approx(0.5f));
    fn.attack(4.f);
    const std::vector<float> out = Copy(fn(2));
    CHECK(out[0] == doctest::Approx(0.75f));
    CHECK(out[1] == doctest::Approx(1.f));
    CHECK(fn.stage() == adsr::DECAY);
  }

  TEST_CASE("adsr: follows the live sample rate (#1064)") {
    ScopedSampleRate rate(2000);
    adsr fn;
    fn.attack(4.f); // 8 samples
    fn.trigger(1.f);
    const std::vector<float> out = Copy(fn());
    for (unsigned int k = 0; k < 8; ++k)
      REQUIRE(out[k] == doctest::Approx(Line(0.f, 1.f, 8, k)));
    YSE::SAMPLERATE = 1000;
    fn.trigger(0.f);
    fn.release(4.f); // now 4 samples
    const std::vector<float> r = Copy(fn());
    CHECK(r[3] == 0.f);
    CHECK(r[2] == doctest::Approx(0.25f));
  }

  TEST_CASE("adsr: settings are cleaned; a non-finite trigger is ignored (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(-3.f).decay(NaN).release(std::numeric_limits<float>::infinity()).retrigger(-1.f);
    CHECK(fn.attack() == 0.f);
    CHECK(fn.decay() == 0.f);
    CHECK(fn.release() == 0.f);
    CHECK(fn.retrigger() == 0.f);
    fn.sustain(NaN);
    CHECK(fn.sustain() == 0.f);
    fn.sustain(2.f);
    CHECK(fn.sustain() == 2.f); // above the peak is allowed
    fn.maxSustain(NaN);
    CHECK(fn.maxSustain() == -1.f);
    fn.maxSustain(-7.f);
    CHECK(fn.maxSustain() == -1.f);
    fn.maxSustain(3.f);
    CHECK(fn.maxSustain() == 3.f);
    fn.maxSustain(-1.f);

    fn.trigger(NaN);
    fn();
    CHECK_FALSE(fn.active());
    fn.trigger(-0.5f); // a negative amplitude: a negative envelope
    fn();
    CHECK(fn.level() == doctest::Approx(-1.f)); // sustain 2 times -0.5
  }

  TEST_CASE("adsr: the latest event before a block wins (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.trigger(1.f).trigger(0.f);
    fn();
    CHECK_FALSE(fn.started());
    CHECK_FALSE(fn.active());
    fn.trigger(0.f).trigger(0.4f);
    fn();
    CHECK(fn.level() == doctest::Approx(0.4f));
  }

  TEST_CASE("adsr: block splits give the same envelope (#1064)") {
    ScopedSampleRate rate(1000);
    const unsigned int total = 300;
    std::vector<float> reference;
    for (unsigned int block : {N, 64u, 13u, 1u}) {
      CAPTURE(block);
      adsr fn;
      fn.attack(17.f).decay(23.f).sustain(0.3f).release(31.f).maxSustain(40.f);
      fn.trigger(0.9f);
      std::vector<float> out;
      for (unsigned int start = 0; start < total; start += block) {
        const unsigned int len = total - start < block ? total - start : block;
        buffer& b = fn(len);
        REQUIRE(b.getLength() == len);
        REQUIRE(fn.gate().getLength() == len);
        for (unsigned int i = 0; i < len; ++i)
          out.push_back(b.getPtr()[i]);
      }
      if (reference.empty()) {
        reference = out;
        CHECK(reference[16] == doctest::Approx(0.9f));
        CHECK(reference[total - 1] == 0.f);
      } else {
        for (unsigned int i = 0; i < total; ++i)
          REQUIRE(out[i] == doctest::Approx(reference[i]));
      }
    }
  }

  TEST_CASE("adsr: reset() returns to rest and drops a pending event (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    fn.attack(10.f).sustain(0.5f);
    fn.trigger(1.f);
    fn();
    fn.trigger(0.f);
    fn.reset();
    CHECK_FALSE(fn.active());
    CHECK(fn.level() == 0.f);
    CHECK(fn.attack() == 10.f);
    fn();
    CHECK_FALSE(fn.finished());
    fn.trigger(1.f);
    const std::vector<float> out = Copy(fn());
    CHECK(out[0] == doctest::Approx(0.1f)); // from 0, no ramp down
  }

  TEST_CASE("adsr: a longer block is processed up to STANDARD_BUFFERSIZE (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    CHECK(fn(N * 2).getLength() == N);
    buffer g(N * 2);
    g = 1.f;
    CHECK(fn(g).getLength() == N);
    CHECK(fn.gate().getLength() == N);
  }

  TEST_CASE("adsr: processing allocates nothing (#1064)") {
    ScopedSampleRate rate(1000);
    adsr fn;
    buffer g = Gate(10, 100, 1.f);
    buffer a(N);
    a = 3.f;
    buffer d(N);
    d = 5.f;
    buffer s(N);
    s = 0.5f;
    buffer r(N);
    r = 7.f;
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn.trigger(1.f);
      fn();
      fn(g);
      fn(a, d, s, r);
      fn(g, a, d, s, r);
      fn(N / 2);
      fn.attack(2.f).decay(3.f).sustain(0.2f).release(4.f).retrigger(1.f).maxSustain(5.f);
      fn.legato(true).playToEnd(true);
      fn.trigger(0.5f);
      fn();
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
