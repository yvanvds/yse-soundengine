// Concurrency stress tests for REVERB::Manager (Phase D of the
// KNOWN_ISSUES.md cross-thread fix).
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

} // TEST_SUITE("reverb")
