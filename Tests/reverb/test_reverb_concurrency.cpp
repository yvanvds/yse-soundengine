// Concurrency stress tests for REVERB::Manager (Phase D of the
// cross-thread fix tracked in issue #41).
//
// REVERB has a simpler shape than SOUND/CHANNEL: there's no `setupJob`
// (reverb impls go straight to OBJECT_SETUP in setup()) and there's no
// audio-thread-iterated per-reverb container of children. The remaining
// races are:
//
//   - main vs slow-pool on `implementations` (now mutex-guarded).
//   - main vs audio-thread on `toLoad` (lock-free inbox).
//
// These tests exercise both paths via rapid reverb create/destroy cycles
// from a single thread and from a worker thread racing with the test
// thread's update() ticks.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "reverb/reverbInterface.hpp"
#include "reverb/reverbManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

  // Drive REVERB::Manager().update() — the only manager we need to
  // stimulate for these tests. We don't tick SOUND/CHANNEL here because
  // the reverb churn is independent of them; the shared slow-pool is
  // drained by REVERB's own deleteJob.
  void drainReverbs(int iterations = 8, int sleepMs = 2) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::REVERB::Manager().update();
      if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  }

} // namespace

TEST_SUITE("reverb") {

  // ─── Single-thread churn ─────────────────────────────────────────────────────

  TEST_CASE("reverb concurrency: single-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
      YSE::reverb r;
      r.create();
      if ((i & 0x0f) == 0) drainReverbs(2);
    }

    drainReverbs(40);
    CHECK(true);
  }

  // ─── Two-thread churn ────────────────────────────────────────────────────────

  TEST_CASE("reverb concurrency: two-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    std::atomic<bool> workerDone{false};
    constexpr int N = 100;

    std::thread worker([&]() {
      for (int i = 0; i < N; ++i) {
        YSE::reverb r;
        r.create();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
      workerDone.store(true, std::memory_order_release);
    });

    int safety = 5000;
    while (!workerDone.load(std::memory_order_acquire) && --safety > 0) {
      drainReverbs(2, 0);
    }
    worker.join();

    drainReverbs(40);
    CHECK(true);
  }

  // ─── Global reverb setters race with update() (issue #192) ───────────────────

  TEST_CASE("reverb concurrency: global reverb setters race with update()") {
    if (!TestHelpers::engineInit()) return;

    // Regression for issue #192. The manager used to copy-assign globalReverb
    // into its audio-thread scratch object (`calculatedValues = globalReverb`),
    // aliasing pimpl. From then on the audio-thread setters on calculatedValues
    // pushed into globalReverb's SPSC message queue while the main thread also
    // produced via the public global-reverb setters — a dual-producer queue.
    // Here the setter thread is the (single, legitimate) producer and update()
    // is the consumer; with the fix there is exactly one producer. Under TSan
    // the old code trips a data race, and the SPSC queue can corrupt/crash even
    // without it.
    YSE::reverb& gr = YSE::REVERB::Manager().getGlobalReverb();
    gr.setActive(true);

    std::atomic<bool> stop{false};
    std::thread setter([&]() {
      const YSE::REVERB_PRESET presets[] = {YSE::REVERB_HALL, YSE::REVERB_CAVE, YSE::REVERB_ROOM,
                                            YSE::REVERB_OFF};
      int i = 0;
      while (!stop.load(std::memory_order_acquire)) {
        gr.setPreset(presets[i & 3]);
        gr.setActive((i & 1) == 0);
        gr.setDryWetBalance(0.5f, 0.5f);
        ++i;
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      }
    });

    // Audio side: no positioned reverbs are in range, so update() takes the
    // partial == 0 steady-state branch that used to alias pimpl.
    for (int i = 0; i < 2000; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::REVERB::Manager().update();
    }

    stop.store(true, std::memory_order_release);
    setter.join();

    // The global reverb is a process-scoped singleton whose state persists
    // across test cases (engineInit() runs System().init() only once). This
    // test mutates its `active` flag, so restore the clean post-init state
    // (active == true) — otherwise the order-dependent case
    // "reverb: Manager global reverb is active after engine init" fails
    // whenever the linker registers this TU first (as on Linux CI).
    gr.setActive(true);

    drainReverbs(40);
    CHECK(gr.getActive() == true);
  }

} // TEST_SUITE("reverb")
