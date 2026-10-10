// Engine-close / offline-render lifecycle tests for YSE::synth (issue #153,
// §8 / §9 of docs/design/synth_core.md).
//
// ISOLATION: these live in their own "synthlifecycle" TEST_SUITE and run as a
// dedicated ctest process, excluded from the combined `yse_unit_tests` run —
// exactly like the "lifecycle" suite. They call System::close(), which
// permanently stops the global thread pools and would break every later suite
// sharing the process. They are ALSO kept out of the "lifecycle" process on
// purpose: that process leaves a lingering file-backed sound impl (the #140
// case), and merely constructing the SYNTH::Manager singleton here reorders
// static teardown enough to expose a pre-existing SOUND/CHANNEL teardown-order
// fragility in that lingering impl. Running the synth close tests in their own
// fresh process sidesteps that entirely. (Tracked as a follow-up issue.)
//
// initOffline() needs no audio hardware, so these run in CI; if it returns
// false on some host the case bails out and doctest counts it as a pass.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "sound/soundInterface.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"
#include "synth/synthManager.h"
#include "internal/time.h"
#include "support/timer_pacing.hpp"

TEST_SUITE("synthlifecycle") {

  // A standalone synth (no sound) survives engine close with notes still held:
  // close() joins the pools, the interface destructs against a closed engine,
  // and the static SYNTH::Manager frees the impl (with its cloned voices) at
  // process exit — no crash.
  TEST_CASE("synthlifecycle: engine close with a standalone synth and held notes") {
    YSE::System().close(); // normalize to a closed engine
    if (!YSE::System().initOffline()) return;
    {
      YSE::SYNTH::sineVoice proto;
      proto.attack(0.005f).release(0.1f);
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      // Voice cloning runs async on the slow pool; wait for it. The budget is
      // counted in deliveries of the suite's reference timer rather than in
      // milliseconds (issue #753), so it stretches with machine load exactly as
      // the slow pool does while a wedged pool still fails the check below.
      TestHelpers::pacedPump(
          2000, [&syn] { return syn.getNumVoices() == 8; },
          [] {
            YSE::INTERNAL::Time().update();
            YSE::SYNTH::Manager().update();
          },
          5);
      CHECK(syn.getNumVoices() == 8);
      for (int n = 60; n < 68; ++n)
        syn.noteOn(1, n, 1.f); // notes queued / held
      YSE::System().close(); // close with the synth still live
    }
    CHECK(true); // interface destructs against the closed engine — no crash
  }

  // A synth attached to a sound, driven to actually sound notes via the offline
  // render loop, then torn down respecting the §9 lifetime contract (the sound
  // is destroyed and drained before the synth). Exercises the #153 lifecycle
  // end to end: create -> attach -> render -> release -> delete.
  TEST_CASE("synthlifecycle: synth behind a sound renders offline then tears down") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto; // outlives everything below (declared first)
    proto.attack(0.005f).decay(0.01f).sustain(0.8f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      {
        YSE::sound snd;
        snd.create(syn);
        // Bring both impls to READY (voice cloning is async on the slow pool,
        // so wait), then render blocks so voices allocate and sound.
        // The wait is budgeted in reference ticks, not milliseconds (#753).
        TestHelpers::pacedPump(
            2000, [&syn] { return syn.getNumVoices() == 8; },
            [] {
              YSE::System().update();
              YSE::System().renderOffline(1);
            },
            5);
        CHECK(syn.getNumVoices() == 8);
        snd.play();
        for (int n = 60; n < 65; ++n)
          syn.noteOn(1, n, 1.f);
        YSE::System().update();
        YSE::System().renderOffline(16); // notes sounding
        snd.stop();
        YSE::System().renderOffline(8);
      } // sound destroyed (notes still held) — synth still alive (§9 order)
      // Drain so the sound impl is fully released + deleted before the synth,
      // over a window of reference ticks rather than of wall clock (#753).
      TestHelpers::pacedPump(
          1000, [] { return false; },
          [] {
            YSE::System().update();
            YSE::System().renderOffline(1);
          },
          5);
    } // synth destroyed after its sound
    // Drain so the synth impl is released + deleted while the engine is up.
    TestHelpers::pacedPump(
        1000, [] { return false; },
        [] {
          YSE::System().update();
          YSE::System().renderOffline(1);
        },
        5);
    YSE::System().close();
    CHECK(true);
  }

} // TEST_SUITE("synthlifecycle")
