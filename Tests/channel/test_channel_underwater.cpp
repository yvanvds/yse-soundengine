// Driver / attach-path tests for the stock underwater effect (issue #327).
//
// The DSP itself is an ordinary insert module (DSP::MODULES::underWater,
// covered in Tests/dsp/test_module_underwater.cpp). These cases verify the
// engine-side default binding:
//   - System().underWaterFX(channel) places the engine's module instance at
//     the head of the channel's insert chain through the ordinary
//     channel::setDSP / ATTACH_DSP message path — the hard-wired slot and
//     bespoke attach path are gone from the channel implementation.
//   - System().setUnderWaterDepth() drives the module's depth parameter as a
//     control-rate write (the "listener depth below the water plane" default
//     driver).
//   - Re-attaching to a different channel severs the previous channel's
//     insert link first, so one module instance is never processed by two
//     channels concurrently.
//
// Engine-dependent cases guard on engineInit() like the rest of the channel
// suite (skipped on CI hosts without an audio device).

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "dsp/dspObject.hpp"
#include "internal/underWaterEffect.h"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

namespace {

  // Pump the (paused) engine until `ready()` holds, so queued channel messages
  // have actually been applied on the manager update path. A fixed iteration
  // count is a bounded window that a loaded slow pool can miss entirely
  // (issue #834); the budget is denominated in reference-timer ticks rather
  // than milliseconds (issue #753), so it stretches with machine load exactly
  // as the background pool does. Returns ready(), so a timed-out wait fails
  // the caller's own assertion.
  //
  // IMPORTANT: `ready()` runs on the test thread between update() calls and may
  // poll only state published on this (the update) thread. `calledfrom` is such
  // state: ATTACH_DSP is applied in sync(), which the manager runs only for
  // impls it has already promoted to OBJECT_READY on this thread, so observing
  // the back-pointer also proves the channel finished its slow-pool setup().
  // Do NOT poll channel::getNumOutputs() here — that reads the impl's `out`
  // vector, which setup() resizes concurrently on the pool (a TSan-confirmed
  // data race on the first #834 attempt).
  template <typename P> bool drainChannelsUntil(P ready, int ticks = 5000) {
    return TestHelpers::pacedPump(
        ticks, ready,
        [] {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
          YSE::CHANNEL::Manager().update();
        },
        2);
  }

} // namespace

TEST_SUITE("channel") {

  TEST_CASE("channel underwater: underWaterFX attaches the module via the ordinary insert path") {
    if (!TestHelpers::engineInit()) return;

    YSE::channel ch;
    ch.create("underwater_attach", YSE::ChannelMaster());
    // No drain needed: getDSP() is an interface-side mirror, and the attach
    // message below waits in the impl's inbox until sync() runs post-READY.
    CHECK(ch.getDSP() == nullptr);

    YSE::System().underWaterFX(ch);
    // The interface mirror reflects the ordinary setDSP path immediately...
    CHECK(ch.getDSP() == &YSE::INTERNAL::UnderWaterEffect().module());

    // ...and after the message pump the impl links the module back
    // (calledfrom is the engine-managed back-pointer addDSP installs). The
    // back-pointer appearing also proves the channel's setup() completed —
    // sync() only runs for READY impls (#834).
    drainChannelsUntil(
        [] { return YSE::INTERNAL::UnderWaterEffect().module().calledfrom != nullptr; });
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().calledfrom != nullptr);

    // Detach so the shared singleton doesn't outlive this test's channel.
    ch.setDSP(nullptr);
    drainChannelsUntil(
        [] { return YSE::INTERNAL::UnderWaterEffect().module().calledfrom == nullptr; });
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().calledfrom == nullptr);
  }

  TEST_CASE("channel underwater: setUnderWaterDepth drives the module's depth control") {
    if (!TestHelpers::engineInit()) return;

    YSE::System().setUnderWaterDepth(3.5f);
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().depth() == doctest::Approx(3.5f));

    // Above the surface clamps to zero (effect fully released).
    YSE::System().setUnderWaterDepth(-2.0f);
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().depth() == doctest::Approx(0.0f));

    YSE::System().setUnderWaterDepth(0.0f);
  }

  TEST_CASE("channel underwater: moving the effect severs the previous channel's insert") {
    if (!TestHelpers::engineInit()) return;

    YSE::channel a;
    YSE::channel b;
    a.create("underwater_move_a", YSE::ChannelMaster());
    b.create("underwater_move_b", YSE::ChannelMaster());
    // No readiness pre-wait: the attach messages queue in each impl's inbox
    // until sync() runs post-READY, and the calledfrom pumps below await both
    // the promotion and the application (#834).

    YSE::System().underWaterFX(a);
    drainChannelsUntil(
        [] { return YSE::INTERNAL::UnderWaterEffect().module().calledfrom != nullptr; });
    YSE::DSP::dspObject** slotA = YSE::INTERNAL::UnderWaterEffect().module().calledfrom;
    REQUIRE(slotA != nullptr);

    YSE::System().underWaterFX(b);
    // Ready when the back-pointer has moved off a's slot onto b's.
    drainChannelsUntil([slotA] {
      YSE::DSP::dspObject** cf = YSE::INTERNAL::UnderWaterEffect().module().calledfrom;
      return cf != nullptr && cf != slotA;
    });
    YSE::DSP::dspObject** slotB = YSE::INTERNAL::UnderWaterEffect().module().calledfrom;

    // Attached to b's impl now, and a's insert slot was cleared on the way —
    // the module is never owned by two channels at once.
    CHECK(slotB != nullptr);
    CHECK(slotB != slotA);
    CHECK(*slotA == nullptr);

    b.setDSP(nullptr);
    drainChannelsUntil(
        [] { return YSE::INTERNAL::UnderWaterEffect().module().calledfrom == nullptr; });
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().calledfrom == nullptr);
  }

} // TEST_SUITE("channel")
