// Concurrency stress tests for CHANNEL::Manager (Phase D of the
// cross-thread fix tracked in issue #41).
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
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "channel/channelImplementation.h"
#include "channel/channelManager.h"
#include "dsp/dspObject.hpp"
#include "patcher/pool_blocker.hpp"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

namespace {

  // Drain CHANNEL + SOUND managers — channels link with sounds and the
  // slow-pool deleteJob is shared, so both managers need ticking. The partial
  // drains inside the cases stay fixed-count on purpose: they are deliberate
  // stress pacing (leaving work in flight is the point), not waits a CHECK
  // depends on.
  void drainChannels(int iterations = 8, int sleepMs = 2) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      YSE::CHANNEL::Manager().update();
      if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  }

  // The completion-signal form of drainChannels(), for the *final* settles: a
  // fixed iteration count is a bounded window a loaded slow pool can miss
  // entirely (issue #835). These settles had to stay fixed-count until #842
  // gave CHANNEL::Manager a synchronized reclamation signal — polling the
  // mutex-guarded implementationCount() between update() ticks is race-free,
  // unlike the unguarded empty() the #834 TSan lesson ruled out. The budget is
  // denominated in reference-timer ticks (issue #753); returns ready(), so a
  // timed-out wait fails the caller's own assertion.
  template <typename P> bool drainUntil(P ready, int ticks = 5000) {
    return TestHelpers::pacedPump(
        ticks, ready,
        [] {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
          YSE::CHANNEL::Manager().update();
        },
        2);
  }

  // True once the canonical implementation list is back at (or below) the
  // count sampled at the head of a case — the reclamation-complete signal the
  // final settles wait on. `<=` rather than `==`: an impl retired by an
  // earlier case may still be reaped during the settle, legitimately lowering
  // the baseline.
  bool reclaimedTo(std::size_t before) {
    return YSE::CHANNEL::Manager().implementationCount() <= before;
  }

  // A no-op insert used as a readiness fence, as in test_channel_metering.cpp:
  // an insert message is applied in sync(), which the manager runs only for an
  // impl it has promoted to OBJECT_READY, and the `calledfrom` back-pointer is
  // written on this (the update) thread. Attach, wait for it, detach, wait for
  // it to clear.
  struct FenceDsp : YSE::DSP::dspObject {
    void create() override {}
    void process(std::vector<YSE::DSP::buffer>&) override {}
  };

  bool awaitChannelReady(YSE::channel& c) {
    // Static so a still-attached fence on the timeout path points at live memory.
    static FenceDsp fence;
    c.setDSP(&fence);
    const bool attached = drainUntil([] { return fence.calledfrom != nullptr; });
    c.setDSP(nullptr);
    const bool detached = drainUntil([] { return fence.calledfrom == nullptr; });
    return attached && detached;
  }

} // namespace

TEST_SUITE("channel") {

  // ─── Regression: a freshly constructed impl is never seen as deletable ───────

  TEST_CASE("channel concurrency: fresh impl status is OBJECT_CONSTRUCTED "
            "regardless of prior memory (issue #336)") {
    // Root cause of the flaky channel two-thread churn hang/segfault: the
    // channel implementationObject ctor left `objectStatus` (a C++17
    // std::atomic, whose default ctor does NOT zero the value) uninitialised.
    // channel::create() emplaces the impl into the mutex-guarded
    // `implementations` list (addImplementation) and only AFTERWARDS calls
    // Manager().setup(), which sets OBJECT_CREATED. During that window the
    // slow-pool deleteJob runs `implementations.remove_if(canBeDeleted)` with
    // canBeDeleted == (objectStatus == OBJECT_DELETE). If the recycled heap slot
    // happened to hold the OBJECT_DELETE bit-pattern, the job freed the node the
    // worker thread was still constructing — a heap-layout-dependent UAF (crash)
    // or corrupted intrusive list (hang). SOUND/REVERB already initialised
    // objectStatus in their ctors; CHANNEL was the outlier.
    //
    // Poison the storage, then placement-construct an impl over it: with the ctor
    // fix objectStatus reads OBJECT_CONSTRUCTED; without it the poison survives
    // and the impl presents as some other (possibly deletable) state. This is
    // deterministic — it fails on the unpatched ctor and passes with the fix.
    alignas(YSE::CHANNEL::implementationObject) unsigned char
        storage[sizeof(YSE::CHANNEL::implementationObject)];
    std::memset(storage, 0xFF, sizeof(storage));
    auto* impl = new (storage) YSE::CHANNEL::implementationObject(nullptr);
    CHECK(impl->getStatus() == YSE::OBJECT_CONSTRUCTED);
    CHECK_FALSE(YSE::CHANNEL::implementationObject::canBeDeleted(*impl));
    impl->~implementationObject();
  }

  // ─── Single-thread churn: many channel create→destroy cycles ─────────────────

  TEST_CASE("channel concurrency: single-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    const std::size_t before = YSE::CHANNEL::Manager().implementationCount();

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
      YSE::channel c;
      c.create(("ch_" + std::to_string(i)).c_str(), YSE::ChannelFX());
      if ((i & 0x0f) == 0) drainChannels(2);
    } // ~channel at end of each iteration releases impl through OBJECT_RELEASE.

    // Final settle: crash-freedom is the assertion, and reclamation actually
    // completing — not a fixed drain count — is the completion signal (#842).
    CHECK(drainUntil([before] { return reclaimedTo(before); }));
  }

  // ─── Two-thread churn: worker creates/destroys while test thread updates ─────

  TEST_CASE("channel concurrency: two-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    const std::size_t before = YSE::CHANNEL::Manager().implementationCount();

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

    // Final settle: reclamation completing is the signal, not a fixed count (#842).
    CHECK(drainUntil([before] { return reclaimedTo(before); }));
  }

  // ─── Regression: a release marked while a delete job is in flight ───────────

  TEST_CASE("channel concurrency: a release marked during an in-flight delete job "
            "is still reclaimed (issue #990)") {
    if (!TestHelpers::engineInit()) return;

    // CHANNEL::Manager().update() used to clear its delete request even when it
    // could not enqueue the delete job because the previous one was still in
    // flight (isQueued() stays true while a job runs). That job may already have
    // walked past the impl the request was for, so the request was lost and the
    // impl stayed in the canonical list until some *other* channel happened to
    // be released — here, never, so the settle below cannot succeed.
    //
    // Staging, per round (the pool has one worker, and its ring is FIFO):
    //   - `x` is created last, so it heads the canonical list and is the first
    //     entry the delete job walks;
    //   - `bulk` is released with the worker parked, and the next tick queues
    //     the delete job for it behind the blocker;
    //   - unparking hands the worker straight to that job, which walks past `x`
    //     first and then spends a long time destroying `bulk`;
    //   - `x` is released and marked on the next tick, and the tick after that
    //     finds the job still running — the tick that used to drop the request.
    // The fixed engine reclaims `x` whatever the interleaving, so the timing
    // below cannot make this case fail spuriously; it only decides whether the
    // unfixed engine is caught (5/5 runs in the debug build).
    constexpr int kBulk = 2000;
    constexpr int kRounds = 5;

    auto tick = [] {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      YSE::CHANNEL::Manager().update();
    };

    for (int round = 0; round < kRounds; ++round) {
      const std::size_t before = YSE::CHANNEL::Manager().implementationCount();

      std::vector<std::unique_ptr<YSE::channel>> bulk;
      bulk.reserve(kBulk);
      for (int i = 0; i < kBulk; ++i) {
        bulk.push_back(std::make_unique<YSE::channel>());
        bulk.back()->create(("bulk_" + std::to_string(i)).c_str(), YSE::ChannelMusic());
      }
      auto x = std::make_unique<YSE::channel>();
      x->create(("x_" + std::to_string(round)).c_str(), YSE::ChannelMusic());
      REQUIRE(awaitChannelReady(*x)); // bulk was set up in the same or an earlier pass

      TestHelpers::PoolBlocker blocker;
      REQUIRE(blocker.Park());
      bulk.clear();
      tick(); // marks bulk OBJECT_DELETE, raises the delete request
      tick(); // queues the delete job behind the blocker
      blocker.Unpark();
      // Let the worker pick the job up and walk past `x` (its first entry)
      // before `x` is marked. The job then runs for tens of milliseconds
      // (measured ~50 ms for 2000 impls in the debug build), so the two ticks
      // below — a few microseconds — land well inside it.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      x.reset();
      tick(); // marks x OBJECT_DELETE while the job is busy with bulk
      tick(); // the job is still in flight: x's request must survive this tick

      const bool reclaimed = drainUntil([before] { return reclaimedTo(before); });
      CHECK_MESSAGE(reclaimed, "round " << round << " stranded a released channel impl");
      if (!reclaimed) break;
    }
  }

  // ─── Nested-release stress: childrenToParent has actual reparenting to do ────

  TEST_CASE("channel concurrency: nested channel release reparents children safely") {
    if (!TestHelpers::engineInit()) return;

    const std::size_t before = YSE::CHANNEL::Manager().implementationCount();

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
    // Final settle: every branch and leaf above must be reclaimed — the list
    // returning to its baseline is the completion signal, not a fixed count (#842).
    CHECK(drainUntil([before] { return reclaimedTo(before); }));
  }

} // TEST_SUITE("channel")
