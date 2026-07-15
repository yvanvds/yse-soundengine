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

namespace {

  // Pump the (paused) engine so queued channel messages are applied on the
  // manager update path — same helper as test_channel_dsp.cpp.
  void drainChannels(int iterations = 8) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      YSE::CHANNEL::Manager().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

} // namespace

TEST_SUITE("channel") {

  TEST_CASE("channel underwater: underWaterFX attaches the module via the ordinary insert path") {
    if (!TestHelpers::engineInit()) return;

    YSE::channel ch;
    ch.create("underwater_attach", YSE::ChannelMaster());
    drainChannels();
    CHECK(ch.getDSP() == nullptr);

    YSE::System().underWaterFX(ch);
    // The interface mirror reflects the ordinary setDSP path immediately...
    CHECK(ch.getDSP() == &YSE::INTERNAL::UnderWaterEffect().module());

    // ...and after the message pump the impl links the module back
    // (calledfrom is the engine-managed back-pointer addDSP installs).
    drainChannels();
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().calledfrom != nullptr);

    // Detach so the shared singleton doesn't outlive this test's channel.
    ch.setDSP(nullptr);
    drainChannels();
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
    drainChannels();

    YSE::System().underWaterFX(a);
    drainChannels();
    YSE::DSP::dspObject** slotA = YSE::INTERNAL::UnderWaterEffect().module().calledfrom;
    REQUIRE(slotA != nullptr);

    YSE::System().underWaterFX(b);
    drainChannels();
    YSE::DSP::dspObject** slotB = YSE::INTERNAL::UnderWaterEffect().module().calledfrom;

    // Attached to b's impl now, and a's insert slot was cleared on the way —
    // the module is never owned by two channels at once.
    CHECK(slotB != nullptr);
    CHECK(slotB != slotA);
    CHECK(*slotA == nullptr);

    b.setDSP(nullptr);
    drainChannels();
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().calledfrom == nullptr);
  }

} // TEST_SUITE("channel")
