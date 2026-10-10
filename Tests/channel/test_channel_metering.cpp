// Tests for the channel output peak-metering API (Phase A of dart-yse#1
// engine support, tracked in yvanvds/yse-soundengine#50).
//
// Covers the API surface on YSE::channel:
//   - getNumOutputs(): 0 on default-constructed channel, > 0 after init.
//   - getPeakLinear*() / getPeakDb*(): silent-state values (linear == 0,
//     dB == -120 floor) on a paused-audio engine.
//   - Bounds: out-of-range outputIdx returns 0 (linear) / -120 (dB).
//   - Pre-built channel parity: master/FX/music/ambient/voice/gui all
//     report a consistent getNumOutputs.
//
// Live-audio metering (volume = 0 muting post peak, sine source producing
// non-zero peak) is covered by an integration-style test that only runs
// under engineInitWithAudio() — skipped on CI without a default device.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include <vector>
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "dsp/dspObject.hpp"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

namespace {
  // The dB floor returned for silent / out-of-range peaks. Kept in sync with
  // the linearToDb helper in channelInterface.cpp.
  constexpr float kDbFloor = -120.f;

  // Channel setup is async: c.create() queues setup() onto the slow-pool;
  // the audio-thread-side promote-from-toLoad pass then sizes `out`. A fixed
  // number of pump iterations is a bounded window that a loaded slow pool can
  // miss entirely (issue #834), so pump both managers until `ready()` — the
  // completion signal — holds. The budget is denominated in reference-timer
  // ticks rather than milliseconds (issue #753), so it stretches with machine
  // load exactly as the pool does. Returns ready(), so a timed-out wait fails
  // the caller's own assertion.
  //
  // IMPORTANT: `ready()` runs on the test thread *between* update() calls and
  // may therefore poll only state that is published on this (the update)
  // thread or otherwise synchronized. In particular it must NOT poll
  // channel::getNumOutputs(): that reads the impl's `out` vector, which the
  // slow-pool setup() resizes concurrently — a data race (TSan-confirmed on
  // the first #834 attempt). Use awaitChannelReady() below as the readiness
  // fence instead, and only read getNumOutputs() after it returned.
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

  // A no-op insert module used purely as a readiness fence. Its process() never
  // runs: the audio thread is paused in this suite.
  struct FenceDsp : YSE::DSP::dspObject {
    void create() override {}
    void process(std::vector<YSE::DSP::buffer>&) override {}
  };

  // Block (paced, #753) until `c`'s implementation has reached OBJECT_READY,
  // without racing the slow pool. The channel interface publishes no
  // synchronized readiness signal of its own, but ATTACH_DSP gives the test
  // one: messages are applied in sync(), which the manager runs only for impls
  // it has already promoted to OBJECT_READY on this (the update) thread — the
  // promotion's acquire load is what publishes the buffers setup() resized. So
  // attach a no-op insert, wait for its `calledfrom` back-pointer (written on
  // this thread) to appear, then detach and wait for it to clear. After a true
  // return, reading getNumOutputs() / the peak getters is an ordinary
  // same-thread read: the slow pool never touches a READY impl's buffers again.
  bool awaitChannelReady(YSE::channel& c) {
    // Static so that on the (loud, kTimerStall-guarded) timeout path a still-
    // attached fence points at live memory instead of a dead stack frame.
    static FenceDsp fence;
    c.setDSP(&fence);
    const bool attached = drainChannelsUntil([] { return fence.calledfrom != nullptr; });
    c.setDSP(nullptr);
    const bool detached = drainChannelsUntil([] { return fence.calledfrom == nullptr; });
    return attached && detached;
  }
} // namespace

TEST_SUITE("channel") {

  // ─── Default-constructed channel: zero outputs, zero peaks ──────────────────

  TEST_CASE("channel metering: default-constructed channel reports zero outputs") {
    YSE::channel c;
    CHECK(c.getNumOutputs() == 0);
  }

  TEST_CASE("channel metering: default-constructed channel returns 0 linear peak") {
    YSE::channel c;
    CHECK(c.getPeakLinearPre() == doctest::Approx(0.f));
    CHECK(c.getPeakLinearPost() == doctest::Approx(0.f));
  }

  TEST_CASE("channel metering: default-constructed channel returns -120 dB peak") {
    YSE::channel c;
    CHECK(c.getPeakDbPre() == doctest::Approx(kDbFloor));
    CHECK(c.getPeakDbPost() == doctest::Approx(kDbFloor));
  }

  TEST_CASE(
      "channel metering: default-constructed channel returns 0 / -120 for any per-output index") {
    YSE::channel c;
    CHECK(c.getPeakLinearPre(0) == doctest::Approx(0.f));
    CHECK(c.getPeakLinearPost(0) == doctest::Approx(0.f));
    CHECK(c.getPeakDbPre(0) == doctest::Approx(kDbFloor));
    CHECK(c.getPeakDbPost(0) == doctest::Approx(kDbFloor));
  }

  // ─── After engine init: master channel has outputs, peaks are silent ─────────

  TEST_CASE("channel metering: master channel reports non-zero output count after init") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelMaster().getNumOutputs() > 0);
  }

  TEST_CASE("channel metering: master channel peaks are silent under paused audio") {
    if (!TestHelpers::engineInit()) return;
    // Audio thread is paused by engineInit(); dsp()/buffersToParent() never
    // ran, so the published peaks remain at their zero-initialised state.
    CHECK(YSE::ChannelMaster().getPeakLinearPre() == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakLinearPost() == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakDbPre() == doctest::Approx(kDbFloor));
    CHECK(YSE::ChannelMaster().getPeakDbPost() == doctest::Approx(kDbFloor));
  }

  TEST_CASE("channel metering: pre-built channels report identical output count") {
    if (!TestHelpers::engineInit()) return;
    // Master is set up synchronously (setMaster), but the five leaf channels
    // are created the same way as user channels — their setup() runs on the
    // slow-pool. Fence on each until it has reached OBJECT_READY and `out` is
    // sized (#834); only then is getNumOutputs() safe to read.
    CHECK(awaitChannelReady(YSE::ChannelFX()));
    CHECK(awaitChannelReady(YSE::ChannelMusic()));
    CHECK(awaitChannelReady(YSE::ChannelAmbient()));
    CHECK(awaitChannelReady(YSE::ChannelVoice()));
    CHECK(awaitChannelReady(YSE::ChannelGui()));
    const int n = YSE::ChannelMaster().getNumOutputs();
    CHECK(YSE::ChannelFX().getNumOutputs() == n);
    CHECK(YSE::ChannelMusic().getNumOutputs() == n);
    CHECK(YSE::ChannelAmbient().getNumOutputs() == n);
    CHECK(YSE::ChannelVoice().getNumOutputs() == n);
    CHECK(YSE::ChannelGui().getNumOutputs() == n);
  }

  TEST_CASE("channel metering: pre-built channels start silent") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelFX().getPeakLinearPost() == doctest::Approx(0.f));
    CHECK(YSE::ChannelMusic().getPeakLinearPost() == doctest::Approx(0.f));
    CHECK(YSE::ChannelAmbient().getPeakLinearPost() == doctest::Approx(0.f));
    CHECK(YSE::ChannelVoice().getPeakLinearPost() == doctest::Approx(0.f));
    CHECK(YSE::ChannelGui().getPeakLinearPost() == doctest::Approx(0.f));
  }

  // ─── User-created channel inherits the device's output layout ───────────────

  TEST_CASE("channel metering: user-created channel inherits the master's output count") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel c;
    c.create("metering_test_channel", YSE::ChannelFX());
    // setup() runs on the slow-pool — fence until the impl actually reaches
    // OBJECT_READY and `out` has been sized from
    // CHANNEL::Manager().getNumberOfOutputs(), rather than hoping a fixed
    // window was wide enough (#834).
    CHECK(awaitChannelReady(c));
    CHECK(c.getNumOutputs() == YSE::ChannelMaster().getNumOutputs());
    CHECK(c.getPeakLinearPost() == doctest::Approx(0.f));
  }

  // ─── Out-of-range output indices return safe defaults ───────────────────────

  TEST_CASE("channel metering: negative outputIdx returns 0 / -120") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelMaster().getPeakLinearPre(-1) == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakLinearPost(-1) == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakDbPre(-1) == doctest::Approx(kDbFloor));
    CHECK(YSE::ChannelMaster().getPeakDbPost(-1) == doctest::Approx(kDbFloor));
  }

  TEST_CASE("channel metering: outputIdx past the end returns 0 / -120") {
    if (!TestHelpers::engineInit()) return;
    const int oob = YSE::ChannelMaster().getNumOutputs() + 32;
    CHECK(YSE::ChannelMaster().getPeakLinearPre(oob) == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakLinearPost(oob) == doctest::Approx(0.f));
    CHECK(YSE::ChannelMaster().getPeakDbPre(oob) == doctest::Approx(kDbFloor));
    CHECK(YSE::ChannelMaster().getPeakDbPost(oob) == doctest::Approx(kDbFloor));
  }

  TEST_CASE("channel metering: per-output peaks at valid indices match the silent state") {
    if (!TestHelpers::engineInit()) return;
    const int n = YSE::ChannelMaster().getNumOutputs();
    REQUIRE(n > 0);
    for (int i = 0; i < n; ++i) {
      CHECK(YSE::ChannelMaster().getPeakLinearPost(i) == doctest::Approx(0.f));
      CHECK(YSE::ChannelMaster().getPeakDbPost(i) == doctest::Approx(kDbFloor));
    }
  }

} // TEST_SUITE("channel")
