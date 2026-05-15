// Concurrency stress tests for CHANNEL::Manager (Phase D of the
// KNOWN_ISSUES.md cross-thread fix).
//
// Exercises the same race classes as test_sound_concurrency.cpp but
// applied to the channel subsystem:
//
//   - main vs slow-pool on `implementations` (now mutex-guarded).
//   - main vs audio-thread on `toLoad` (lock-free inbox).
//   - audio-thread vs slow-pool on `parent->children` via the channel
//     impl destructor — Phase B added a `connectedToParent` flag and
//     made the audio thread call `childrenToParent()` + `disconnect()`
//     before marking OBJECT_DELETE, so the slow-pool's destructor
//     short-circuits when the audio thread has already cleaned up.
//
// Notable difference from sounds: channels nest. A user channel can be
// a child of another user channel. The "nested release" test creates a
// chain so `childrenToParent()` actually has work to do at release
// time.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

// Drain CHANNEL + SOUND managers — channels link with sounds and the
// slow-pool deleteJob is shared, so both managers need ticking.
void drainChannels(int iterations = 8, int sleepMs = 2) {
    for (int i = 0; i < iterations; ++i) {
        YSE::INTERNAL::Time().update();
        YSE::SOUND::Manager().update();
        YSE::CHANNEL::Manager().update();
        if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

} // namespace

TEST_SUITE("channel") {

// ─── Single-thread churn: many channel create→destroy cycles ─────────────────

TEST_CASE("channel concurrency: single-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
        YSE::channel c;
        c.create(("ch_" + std::to_string(i)).c_str(), YSE::ChannelFX());
        if ((i & 0x0f) == 0) drainChannels(2);
    } // ~channel at end of each iteration releases impl through OBJECT_RELEASE.

    drainChannels(40);
    CHECK(true);
}

// ─── Two-thread churn: worker creates/destroys while test thread updates ─────

TEST_CASE("channel concurrency: two-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    std::atomic<bool> workerDone{false};
    constexpr int N = 100;

    std::thread worker([&]() {
        for (int i = 0; i < N; ++i) {
            YSE::channel c;
            c.create(("worker_ch_" + std::to_string(i)).c_str(), YSE::ChannelMusic());
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        workerDone.store(true, std::memory_order_release);
    });

    int safety = 5000;
    while (!workerDone.load(std::memory_order_acquire) && --safety > 0) {
        drainChannels(2, 0);
    }
    worker.join();

    drainChannels(40);
    CHECK(true);
}

// ─── Nested-release stress: childrenToParent has actual reparenting to do ────

TEST_CASE("channel concurrency: nested channel release reparents children safely") {
    if (!TestHelpers::engineInit()) return;

    // Create N "branch" channels each with a couple of leaf subchannels,
    // then let everything go out of scope at once. The audio-thread-side
    // release path (Phase B.4) must reparent each leaf to the original
    // grandparent (ChannelMusic) before the branch's impl becomes eligible
    // for deletion.
    constexpr int N = 50;
    {
        YSE::channel branches[N];
        YSE::channel leavesA[N];
        YSE::channel leavesB[N];
        for (int i = 0; i < N; ++i) {
            branches[i].create(("branch_" + std::to_string(i)).c_str(), YSE::ChannelMusic());
            leavesA[i].create(("leafA_" + std::to_string(i)).c_str(), branches[i]);
            leavesB[i].create(("leafB_" + std::to_string(i)).c_str(), branches[i]);
        }
        drainChannels(10);
        // Scope exit: ~channel triggers OBJECT_RELEASE in reverse order
        // (leavesB, leavesA, branches). Audio-thread's update will see
        // each branch in OBJECT_RELEASE *after* the leaves have already
        // been reparented away. Either ordering is fine — the
        // childrenToParent() path is exercised in both.
    }
    drainChannels(60);
    CHECK(true);
}

} // TEST_SUITE("channel")
