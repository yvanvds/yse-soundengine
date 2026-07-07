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

} // TEST_SUITE("panner")
