// Tests for surround speaker-layout configuration in the channel manager
// (issue #203). Covers the three defects the issue documents, all in
// YseEngine/channel/channelManager.cpp:
//
//   1. Uninitialized speaker angle — every allocated output must read a
//      defined angle (value-initialised array), not indeterminate memory.
//   2. No LFE concept — the .1 output must be flagged so it is excluded from
//      azimuth panning (getOutputIsLFE()).
//   3. Wrong physical channel order — 5.1/6.1/7.1 must follow the platform
//      standard order (FL FR FC LFE ...), with the LFE at index 3.
//
// These drive the real CHANNEL::Manager() through its public
// setChannelConf()/changeChannelConf() API and read back the per-output angle
// and LFE flag. Like the sibling channel tests they guard on engineInit()
// (which pauses the audio stream, so the test thread is the sole caller) and
// each case restores the default stereo layout so later tests see the engine
// in its initial state.

#include <doctest/doctest.h>
#include <cmath>
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "headers/enums.hpp"
#include "support/null_device.hpp"

namespace {

  // Same conversion the manager uses (utils/misc.hpp Pi = 3.14159265f).
  constexpr float kPi = 3.14159265f;
  float rad(float degrees) {
    return kPi / 180.0f * degrees;
  }

  // Restore the layout System().init() leaves behind so the shared engine
  // singleton looks untouched to subsequent test cases.
  void restoreStereo() {
    YSE::CHANNEL::Manager().setChannelConf(YSE::CT_STEREO, 2);
    YSE::CHANNEL::Manager().changeChannelConf();
  }

  void applyLayout(YSE::CHANNEL_TYPE type, int outputs) {
    YSE::CHANNEL::Manager().setChannelConf(type, outputs);
    YSE::CHANNEL::Manager().changeChannelConf();
  }

} // namespace

TEST_SUITE("channel") {

  // ─── 5.1: LFE at index 3, platform channel order ─────────────────────────────

  TEST_CASE("channel layout: CT_51 uses FL FR FC LFE BL BR order with LFE at index 3 (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_51, 6);

    CHECK(m.getNumberOfOutputs() == 6);

    // Only index 3 is the LFE.
    CHECK(m.getOutputIsLFE(3) == true);
    CHECK(m.getOutputIsLFE(0) == false);
    CHECK(m.getOutputIsLFE(1) == false);
    CHECK(m.getOutputIsLFE(2) == false);
    CHECK(m.getOutputIsLFE(4) == false);
    CHECK(m.getOutputIsLFE(5) == false);

    // Angles: FL FR FC (LFE=0) BL BR.
    CHECK(m.getOutputAngle(0) == doctest::Approx(rad(-45.f)));
    CHECK(m.getOutputAngle(1) == doctest::Approx(rad(45.f)));
    CHECK(m.getOutputAngle(2) == doctest::Approx(rad(0.f)));
    CHECK(m.getOutputAngle(3) == doctest::Approx(0.f)); // LFE is not panned
    CHECK(m.getOutputAngle(4) == doctest::Approx(rad(-135.f)));
    CHECK(m.getOutputAngle(5) == doctest::Approx(rad(135.f)));

    restoreStereo();
  }

  // ─── 7.1: LFE at index 3, 8-channel order ────────────────────────────────────

  TEST_CASE("channel layout: CT_71 uses FL FR FC LFE BL BR SL SR order (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_71, 8);

    CHECK(m.getNumberOfOutputs() == 8);

    CHECK(m.getOutputIsLFE(3) == true);
    for (unsigned i = 0; i < 8; ++i) {
      if (i != 3) CHECK(m.getOutputIsLFE(i) == false);
    }

    CHECK(m.getOutputAngle(0) == doctest::Approx(rad(-45.f)));
    CHECK(m.getOutputAngle(1) == doctest::Approx(rad(45.f)));
    CHECK(m.getOutputAngle(2) == doctest::Approx(rad(0.f)));
    CHECK(m.getOutputAngle(3) == doctest::Approx(0.f)); // LFE
    CHECK(m.getOutputAngle(4) == doctest::Approx(rad(-135.f)));
    CHECK(m.getOutputAngle(5) == doctest::Approx(rad(135.f)));
    CHECK(m.getOutputAngle(6) == doctest::Approx(rad(-90.f)));
    CHECK(m.getOutputAngle(7) == doctest::Approx(rad(90.f)));

    restoreStereo();
  }

  // ─── 6.1: LFE at index 3, 7-channel order ────────────────────────────────────

  TEST_CASE("channel layout: CT_61 uses FL FR FC LFE SL SR BC order (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_61, 7);

    CHECK(m.getNumberOfOutputs() == 7);

    CHECK(m.getOutputIsLFE(3) == true);
    for (unsigned i = 0; i < 7; ++i) {
      if (i != 3) CHECK(m.getOutputIsLFE(i) == false);
    }

    CHECK(m.getOutputAngle(0) == doctest::Approx(rad(-45.f)));
    CHECK(m.getOutputAngle(1) == doctest::Approx(rad(45.f)));
    CHECK(m.getOutputAngle(2) == doctest::Approx(rad(0.f)));
    CHECK(m.getOutputAngle(3) == doctest::Approx(0.f)); // LFE
    CHECK(m.getOutputAngle(4) == doctest::Approx(rad(-90.f)));
    CHECK(m.getOutputAngle(5) == doctest::Approx(rad(90.f)));
    CHECK(m.getOutputAngle(6) == doctest::Approx(rad(180.f)));

    restoreStereo();
  }

  // ─── CT_AUTO maps a 6-channel device to 5.1 (regression: index 5 used to be
  //     uninitialized, and there was no LFE) ─────────────────────────────────────

  TEST_CASE("channel layout: CT_AUTO on a 6-channel device yields 5.1 with LFE at 3 (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_AUTO, 6);

    CHECK(m.getNumberOfOutputs() == 6);
    CHECK(m.getOutputIsLFE(3) == true);

    // Every allocated output is defined — the last output (index 5) in
    // particular used to be left indeterminate by the old setAuto/set51 path.
    CHECK(m.getOutputAngle(4) == doctest::Approx(rad(-135.f)));
    CHECK(m.getOutputAngle(5) == doctest::Approx(rad(135.f)));

    restoreStereo();
  }

  // ─── Non-.1 layouts carry no LFE, and bounds are safe ────────────────────────

  TEST_CASE("channel layout: CT_QUAD has no LFE and correct corner angles (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_QUAD, 4);

    CHECK(m.getNumberOfOutputs() == 4);
    for (unsigned i = 0; i < 4; ++i)
      CHECK(m.getOutputIsLFE(i) == false);

    CHECK(m.getOutputAngle(0) == doctest::Approx(rad(-45.f)));
    CHECK(m.getOutputAngle(1) == doctest::Approx(rad(45.f)));
    CHECK(m.getOutputAngle(2) == doctest::Approx(rad(-135.f)));
    CHECK(m.getOutputAngle(3) == doctest::Approx(rad(135.f)));

    restoreStereo();
  }

  TEST_CASE("channel layout: stereo has no LFE and out-of-range queries are safe (#203)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_STEREO, 2);

    CHECK(m.getNumberOfOutputs() == 2);
    CHECK(m.getOutputIsLFE(0) == false);
    CHECK(m.getOutputIsLFE(1) == false);

    // Out-of-range indices return safe defaults rather than reading past the end.
    CHECK(m.getOutputIsLFE(2) == false);
    CHECK(m.getOutputIsLFE(999) == false);
    CHECK(m.getOutputAngle(2) == doctest::Approx(0.f));
    CHECK(m.getOutputAngle(999) == doctest::Approx(0.f));

    restoreStereo();
  }

} // TEST_SUITE("channel")
