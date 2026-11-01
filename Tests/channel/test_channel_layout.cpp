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
#include <vector>
#include "channel/channelInterface.hpp"
#include "channel/channelImplementation.h"
#include "channel/channelManager.h"
#include "sound/soundImplementation.h"
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

  // Build a CHANNEL::output for a speaker at a given angle (radians).
  YSE::CHANNEL::output speaker(float angleRad, bool lfe = false) {
    YSE::CHANNEL::output o;
    o.angle = angleRad;
    o.isLFE = lfe;
    return o;
  }

  // Reference implementation of effective[i]: the exact inline loop
  // toChannels() ran before issue #211 — Σ over non-LFE speakers j of the
  // cardioid overlap weight. Independent of the code under test so the
  // equivalence check is meaningful.
  float refEffective(const std::vector<YSE::CHANNEL::output>& conf, std::size_t i) {
    float sum = 0.f;
    for (std::size_t j = 0; j < conf.size(); ++j) {
      if (conf[j].isLFE) continue;
      sum += YSE::SOUND::implementationObject::computeSpeakerOverlap(conf[i].angle, conf[j].angle);
    }
    return sum;
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

  // ─── A wider count is never published over narrower arrays (#1222) ────────
  //
  // openDevice() calls setChannelConf() with the device's output count (128 on
  // a PipeWire ALSA PCM, #1214) on the control thread, and changeChannelConf()
  // only runs later, from the audio callback. Every reader in between — a
  // slow-pool CHANNEL::implementationObject::setup(), a panner — loops to
  // getNumberOfOutputs() and indexes getOutputAngle()/getOutputIsLFE(). Those
  // read the arrays the previous changeChannelConf() sized, two entries wide
  // after initShared(), so ASan reported a heap-buffer-overflow at index 2.
  // Here no callback runs (engineInit() pauses the stream), so the window is
  // held open deterministically: every index below the published count must
  // read in bounds, as 0° / not-LFE until the audio thread applies the preset.

  TEST_CASE("channel layout: outputs published by setChannelConf read in bounds before "
            "changeChannelConf (#1222)") {
    if (!TestHelpers::engineInit()) return;
    auto& m = YSE::CHANNEL::Manager();
    applyLayout(YSE::CT_STEREO, 2);

    // The 2 -> 128 widening from the issue, then a further widening while the
    // first is still unapplied, so a count beyond any earlier layout is covered
    // too.
    for (const int outputs : {128, 200}) {
      m.setChannelConf(YSE::CT_AUTO, outputs);
      REQUIRE(m.getNumberOfOutputs() == static_cast<UInt>(outputs));
      bool angleInRange = true;
      bool lfeClear = true;
      for (UInt i = 2; i < m.getNumberOfOutputs(); ++i) {
        if (m.getOutputAngle(i) != 0.f) angleInRange = false;
        if (m.getOutputIsLFE(i)) lfeClear = false;
      }
      CHECK(angleInRange);
      CHECK(lfeClear);
    }

    // The audio thread's half: no allocation, just the preset on the layout
    // setChannelConf() sized. 200 outputs have no defined order, so stereo.
    YSE::CHANNEL::Manager().changeChannelConf();
    CHECK(m.getNumberOfOutputs() == 200u);
    CHECK(m.getOutputAngle(0) == doctest::Approx(rad(-90.f)));
    CHECK(m.getOutputAngle(1) == doctest::Approx(rad(90.f)));
    CHECK(m.getOutputAngle(199) == doctest::Approx(0.f));
    CHECK(m.getOutputAngle(200) == doctest::Approx(0.f));

    // Narrowing reuses the wide layout; a value an earlier, wider preset left
    // behind must not leak into the narrower one.
    applyLayout(YSE::CT_51, 6);
    applyLayout(YSE::CT_QUAD, 4);
    CHECK(m.getNumberOfOutputs() == 4u);
    for (UInt i = 0; i < 4; ++i)
      CHECK(m.getOutputIsLFE(i) == false);
    CHECK(m.getOutputAngle(4) == doctest::Approx(0.f));

    restoreStereo();
  }

  // ─── Stream width for a layout (#1214) ───────────────────────────────────────
  //
  // openDevice() used to open every output a device advertises, whatever the
  // layout. ALSA's `default` and `pipewire` PCMs report 128 on a PipeWire
  // desktop, so a stereo or auto open sized the master to 128 outputs.
  // outputsForLayout() is the stream width openDevice() and addCallback() now
  // use; it is pure, so it is driven without an engine.

  TEST_CASE("channel layout: a named layout opens its own speaker count, clamped to the device "
            "(#1214)") {
    using M = YSE::CHANNEL::managerObject;
    CHECK(M::outputsForLayout(YSE::CT_MONO, 128) == 1);
    CHECK(M::outputsForLayout(YSE::CT_STEREO, 128) == 2);
    CHECK(M::outputsForLayout(YSE::CT_QUAD, 128) == 4);
    CHECK(M::outputsForLayout(YSE::CT_51, 128) == 6);
    CHECK(M::outputsForLayout(YSE::CT_51SIDE, 128) == 6);
    CHECK(M::outputsForLayout(YSE::CT_61, 128) == 7);
    CHECK(M::outputsForLayout(YSE::CT_71, 128) == 8);
    // Never wider than the device: a 5.1 request on a stereo device is two.
    CHECK(M::outputsForLayout(YSE::CT_51, 2) == 2);
    CHECK(M::outputsForLayout(YSE::CT_STEREO, 1) == 1);
    CHECK(M::outputsForLayout(YSE::CT_STEREO, 0) == 0);
    CHECK(M::outputsForLayout(YSE::CT_STEREO, -1) == 0);
  }

  TEST_CASE("channel layout: CT_AUTO opens the outputs setAuto places, CT_CUSTOM all of them "
            "(#1214)") {
    using M = YSE::CHANNEL::managerObject;
    for (const int n : {1, 2, 4, 6, 7, 8})
      CHECK(M::outputsForLayout(YSE::CT_AUTO, n) == n);
    // Counts setAuto() places as stereo: only two outputs carry sound.
    for (const int n : {3, 5, 9, 16, 128})
      CHECK(M::outputsForLayout(YSE::CT_AUTO, n) == 2);
    CHECK(M::outputsForLayout(YSE::CT_AUTO, 0) == 0);
    CHECK(M::outputsForLayout(YSE::CT_CUSTOM, 128) == 128);
    CHECK(M::outputsForLayout(YSE::CT_CUSTOM, 3) == 3);
  }

  // ─── Precomputed speaker-density weights (#211) ──────────────────────────────
  //
  // effective[i] = Σ_j overlap(angle_i, angle_j) over the non-LFE speakers is
  // now precomputed once per layout change in
  // CHANNEL::implementationObject::computeEffectiveSpeakerWeights() instead of
  // being recomputed per source channel per block inside toChannels(). These
  // drive the pure static directly (no engine needed) and pin the two
  // properties that make the refactor safe: (1) it produces exactly the value
  // the old inline loop did, and (2) it is a no-op on symmetric layouts — the
  // whole reason the per-block recompute was pure waste there.

  TEST_CASE("effective weights: match the old inline loop on an asymmetric ring (#211)") {
    // A strongly asymmetric CT_CUSTOM ring (three speakers clustered left, one
    // right) — exactly where the density term is non-trivial and must be right.
    std::vector<YSE::CHANNEL::output> conf = {speaker(rad(-150.f)), speaker(rad(-120.f)),
                                              speaker(rad(-90.f)), speaker(rad(90.f))};
    // Compute the reference from the untouched geometry BEFORE the code under
    // test overwrites effective.
    std::vector<float> expected(conf.size());
    for (std::size_t i = 0; i < conf.size(); ++i)
      expected[i] = refEffective(conf, i);

    YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(conf);

    for (std::size_t i = 0; i < conf.size(); ++i)
      CHECK(conf[i].effective == doctest::Approx(expected[i]));
    // The whole point of the asymmetric case: the weights are genuinely
    // non-uniform (otherwise the term would cancel and there'd be nothing to
    // get wrong). The clustered-left speakers overlap more, so score higher
    // than the lone right speaker.
    CHECK(conf[0].effective > conf[3].effective);
  }

  TEST_CASE("effective weights: uniform on a symmetric layout — the no-op case (#211)") {
    // Stereo (±90°): both speakers see the same neighbourhood, so effective is
    // identical across them and cancels in the power normalisation. This is the
    // symmetric-layout no-op the issue calls out.
    std::vector<YSE::CHANNEL::output> stereo = {speaker(rad(-90.f)), speaker(rad(90.f))};
    YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(stereo);
    CHECK(stereo[0].effective == doctest::Approx(stereo[1].effective));

    // Quad (±45°, ±135°): a regular ring — every speaker's weight is equal too.
    std::vector<YSE::CHANNEL::output> quad = {speaker(rad(-45.f)), speaker(rad(45.f)),
                                              speaker(rad(-135.f)), speaker(rad(135.f))};
    YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(quad);
    for (std::size_t i = 1; i < quad.size(); ++i)
      CHECK(quad[i].effective == doctest::Approx(quad[0].effective));
  }

  TEST_CASE("effective weights: LFE outputs are excluded and left untouched (#211)") {
    // 5.1-style ordering with the LFE at index 3. The LFE must neither
    // contribute to any positional speaker's sum nor receive a computed weight —
    // matching toChannels(), which skips it in both loops (issue #203).
    std::vector<YSE::CHANNEL::output> conf = {speaker(rad(-45.f)),  speaker(rad(45.f)),
                                              speaker(rad(0.f)),    speaker(rad(0.f), /*lfe=*/true),
                                              speaker(rad(-135.f)), speaker(rad(135.f))};

    // Reference computed with the LFE excluded from the j-sum.
    std::vector<float> expected(conf.size());
    for (std::size_t i = 0; i < conf.size(); ++i)
      expected[i] = refEffective(conf, i);

    // The LFE's effective must stay at its default (never written).
    const float lfeDefault = conf[3].effective;

    YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(conf);

    for (std::size_t i = 0; i < conf.size(); ++i) {
      if (conf[i].isLFE) continue;
      CHECK(conf[i].effective == doctest::Approx(expected[i]));
    }
    CHECK(conf[3].effective == doctest::Approx(lfeDefault)); // LFE untouched
  }

} // TEST_SUITE("channel")
