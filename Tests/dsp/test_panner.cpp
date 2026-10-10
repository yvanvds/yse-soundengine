// Unit tests for the extracted DSP::panner spatialization component (issue
// #169). Two things are proven here, both without any audio device:
//
//   1. The pure pan-math helpers moved out of SOUND::implementationObject into
//      DSP::panner are bit-identical to what the sound path still exposes — the
//      sound methods now forward to the panner, so a mismatch would mean the
//      "one shared copy, cannot drift" promise of the design (§6) was broken.
//   2. The moved math still upholds the spatializer review invariants the design
//      requires the extraction to preserve (#202 antipodal-NaN guard, #204 angle
//      wrap / no relative mirror, #207 overlap, #208 doppler, #210 zenith).
//
// The per-instance panner (resize/update/spread over a live speaker layout) is
// exercised in Tests/synth/test_synth_positioning.cpp, which needs an offline
// engine for the device layout.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>

#include "yse.hpp"
#include "dsp/panner.hpp"
#include "sound/soundImplementation.h"

using Panner = YSE::DSP::panner;
using SoundImpl = YSE::SOUND::implementationObject;

TEST_SUITE("panner") {

  // ─── forwarder equivalence: DSP::panner == SOUND::implementationObject ──────
  // The sound helpers are now thin forwarders; these guard against a future edit
  // re-forking the math.

  TEST_CASE("panner: sound helpers forward to DSP::panner bit-identically") {
    for (float a = -3.f; a <= 3.f; a += 0.37f) {
      for (float b = -3.f; b <= 3.f; b += 0.53f) {
        CHECK(Panner::computeSpeakerOverlap(a, b) == SoundImpl::computeSpeakerOverlap(a, b));
      }
    }
    for (unsigned n = 1; n <= 8; ++n) {
      for (float g = 0.f; g <= 1.f; g += 0.13f) {
        for (float p = 0.f; p <= 2.f; p += 0.29f) {
          CHECK(Panner::computePanRatio(g, p, n) == SoundImpl::computePanRatio(g, p, n));
        }
      }
    }
    for (float x = -2.f; x <= 2.f; x += 0.5f) {
      for (float z = -2.f; z <= 2.f; z += 0.5f) {
        YSE::Pos dir(x, 0.3f, z);
        YSE::Pos fwd(0.f, 0.f, 1.f);
        CHECK(Panner::computeSourceAngle(false, dir, fwd) ==
              SoundImpl::computeSourceAngle(false, dir, fwd));
        CHECK(Panner::computeSourceAngle(true, dir, fwd) ==
              SoundImpl::computeSourceAngle(true, dir, fwd));
        CHECK(Panner::computeHorizontalFraction(dir) == SoundImpl::computeHorizontalFraction(dir));
      }
    }
    CHECK(Panner::computeVirtualDist(5.f, 1.f, 0.8f) ==
          SoundImpl::computeVirtualDist(5.f, 1.f, 0.8f));
    YSE::Pos sv(1.f, 0.f, 0.f), lv(0.f, 0.f, 0.f), d(3.f, 0.f, 0.f);
    CHECK(Panner::computeDopplerRatio(sv, lv, d, 1.f) ==
          SoundImpl::computeDopplerRatio(sv, lv, d, 1.f));
    YSE::Pos last(0.f, 0.f, 0.f), now(1.f, 2.f, 3.f), prev(0.5f, 0.5f, 0.5f);
    for (int i = 0; i <= 5; ++i) { // i == 0 gives dt == 0 on purpose
      const float dt = 0.01f * static_cast<float>(i);
      CHECK(Panner::computeVelocity(now, last, dt, prev).x ==
            SoundImpl::computeVelocity(now, last, dt, prev).x);
      CHECK(Panner::computeVelocity(now, last, dt, prev).z ==
            SoundImpl::computeVelocity(now, last, dt, prev).z);
    }
  }

  TEST_CASE("panner: gainAccumulate matches the sound forwarder sample-for-sample") {
    const UInt len = 128;
    std::vector<float> src(len), fader(len, 0.9f);
    for (UInt i = 0; i < len; ++i)
      src[i] = std::sin(0.05f * static_cast<float>(i));
    std::vector<float> destA(len, 0.f), destB(len, 0.f);
    float lastA = 0.2f, lastB = 0.2f;
    Panner::gainAccumulate(src.data(), fader.data(), destA.data(), len, lastA, 0.7f);
    SoundImpl::gainAccumulate(src.data(), fader.data(), destB.data(), len, lastB, 0.7f);
    CHECK(lastA == lastB);
    for (UInt i = 0; i < len; ++i)
      CHECK(destA[i] == destB[i]);
  }

  // ─── preserved spatializer invariants ──────────────────────────────────────

  TEST_CASE("panner: antipodal / zero power falls back to a finite equal split (#202)") {
    // power collapses to 0 -> must NOT return NaN, but 1/N.
    float r = Panner::computePanRatio(0.f, 0.f, 4);
    CHECK(std::isfinite(r));
    CHECK(r == doctest::Approx(0.25f));
    CHECK(Panner::computePanRatio(0.5f, 0.f, 0) == 0.f); // no speakers
  }

  TEST_CASE("panner: source angle wraps to (-pi, pi] and does not mirror relative (#204)") {
    YSE::Pos right(1.f, 0.f, 0.f), fwd(0.f, 0.f, 1.f);
    // +x maps to +90 degrees on both frames; the relative branch must not negate.
    CHECK(Panner::computeSourceAngle(false, right, fwd) ==
          doctest::Approx(static_cast<float>(YSE::Pi) * 0.5f));
    CHECK(Panner::computeSourceAngle(true, right, fwd) ==
          doctest::Approx(static_cast<float>(YSE::Pi) * 0.5f));
  }

  TEST_CASE(
      "panner: horizontal fraction is 1 on the horizon and shrinks toward the zenith (#210)") {
    CHECK(Panner::computeHorizontalFraction(YSE::Pos(1.f, 0.f, 0.f)) == doctest::Approx(1.f));
    CHECK(Panner::computeHorizontalFraction(YSE::Pos(0.f, 0.f, 0.f)) == doctest::Approx(1.f));
    float overhead = Panner::computeHorizontalFraction(YSE::Pos(0.01f, 10.f, 0.01f));
    CHECK(overhead < 0.1f);
  }

  TEST_CASE("panner: doppler ratio is 1 when nothing moves and clamps a supersonic close (#208)") {
    YSE::Pos still(0.f, 0.f, 0.f), d(3.f, 0.f, 0.f);
    CHECK(Panner::computeDopplerRatio(still, still, d, 1.f) == doctest::Approx(1.f));
    YSE::Pos fast(-1000.f, 0.f, 0.f); // closing far faster than sound
    float r = Panner::computeDopplerRatio(fast, still, d, 1.f);
    CHECK(std::isfinite(r));
    CHECK(r <= 4.0f);
    CHECK(r >= 0.25f);
  }

  // ─── zero-length update tick (#660) ────────────────────────────────────────
  //
  // A tick that measured no time reports delta == 0. That was routine while
  // INTERNAL::Time() ran on the millisecond-quantised std::clock() (two update
  // ticks inside the same millisecond); on the monotonic clock of #667 it is
  // rare but still reachable — the clock's own seeding tick, and any two
  // updates landing inside one clock period — so these guards are still the
  // backstop. The old velocity derivation divided by delta unconditionally:
  // 1/0 is +inf, and a *stationary* source
  // (newPos == lastPos) then computes 0 * inf == NaN. Nothing downstream
  // rejects it — every guard on the way to the playback rate is an ordinary
  // comparison, all false for NaN — so the playhead latched at NaN for the rest
  // of playback. The two guards below are the fix; the user-visible symptom is
  // covered end-to-end in Tests/sound/test_sound_doppler.cpp.

  TEST_CASE("panner: a normal tick derives velocity as distance over delta (#660)") {
    YSE::Pos from(0.f, 0.f, 0.f), to(2.f, -1.f, 0.5f), prev(9.f, 9.f, 9.f);
    YSE::Pos v = Panner::computeVelocity(to, from, 0.5f, prev);
    CHECK(v.x == doctest::Approx(4.f));
    CHECK(v.y == doctest::Approx(-2.f));
    CHECK(v.z == doctest::Approx(1.f));
  }

  TEST_CASE("panner: a zero-length tick holds the previous velocity instead of NaN (#660)") {
    YSE::Pos stationary(3.f, 0.f, 0.f), prev(1.5f, 0.f, 0.f);
    // Pre-fix this was (0,0,0) * inf == (NaN,NaN,NaN).
    YSE::Pos v = Panner::computeVelocity(stationary, stationary, 0.f, prev);
    CHECK(std::isfinite(v.x));
    CHECK(v.x == doctest::Approx(1.5f));
    CHECK(v.y == doctest::Approx(0.f));
    CHECK(v.z == doctest::Approx(0.f));

    // A moving source over a zero-length tick would have been +inf, which
    // survives the doppler clamps as maxRatio rather than as a measurement.
    YSE::Pos moved(4.f, 0.f, 0.f);
    YSE::Pos m = Panner::computeVelocity(moved, stationary, 0.f, prev);
    CHECK(std::isfinite(m.x));
    CHECK(m.x == doctest::Approx(1.5f));
  }

  TEST_CASE("panner: a negative or NaN tick length holds the previous velocity too (#660)") {
    YSE::Pos from(0.f, 0.f, 0.f), to(1.f, 0.f, 0.f), prev(0.25f, 0.f, 0.f);
    CHECK(Panner::computeVelocity(to, from, -0.01f, prev).x == doctest::Approx(0.25f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    YSE::Pos v = Panner::computeVelocity(to, from, nan, prev);
    CHECK(std::isfinite(v.x));
    CHECK(v.x == doctest::Approx(0.25f));
  }

  TEST_CASE("panner: a non-finite velocity cannot escape as the doppler ratio (#660)") {
    // Defence in depth for the second half of the same failure: the clamps in
    // computeDopplerRatio are comparisons, so before the guard a NaN velocity
    // came back out as a NaN ratio and became the playback rate.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    YSE::Pos still(0.f, 0.f, 0.f), d(3.f, 0.f, 0.f);
    CHECK(std::isfinite(Panner::computeDopplerRatio(YSE::Pos(nan, nan, nan), still, d, 1.f)));
    CHECK(Panner::computeDopplerRatio(YSE::Pos(nan, nan, nan), still, d, 1.f) ==
          doctest::Approx(1.f));
    CHECK(std::isfinite(Panner::computeDopplerRatio(still, YSE::Pos(nan, 0.f, 0.f), d, 1.f)));
    CHECK(std::isfinite(Panner::computeDopplerRatio(still, still, YSE::Pos(nan, 0.f, 0.f), 1.f)));
    CHECK(std::isfinite(Panner::computeDopplerRatio(YSE::Pos(inf, 0.f, 0.f), still, d, 1.f)));
    CHECK(std::isfinite(Panner::computeDopplerRatio(still, still, d, nan)));
  }

} // TEST_SUITE("panner")
