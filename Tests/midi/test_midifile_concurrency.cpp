// Concurrency stress tests for MIDI::Manager (issue #190).
//
// Before the fix, `MIDI::managerObject` kept a single `forward_list` that the
// main thread mutated (emplace_front from the `MIDI::file` constructor) while
// the audio thread walked and erase_after'd it in update(), and `fileImpl::head`
// was a plain pointer nulled by the main thread while the audio thread read it.
// Creating/destroying `MIDI::file` objects while the engine ticked update() was
// therefore a data race / use-after-free.
//
// The fix mirrors the reverb/channel managers:
//   - canonical `implementations` list guarded by a mutex (main vs slow-pool),
//   - a lock-free SPSC inbox handing new impls to the audio thread,
//   - an audio-thread-owned `inUse` working list,
//   - a slow-pool deleteJob that reaps OBJECT_DELETE impls (never freed on the
//     audio thread),
//   - `head` promoted to std::atomic.
//
// These tests churn create/destroy from one and then two threads while the test
// thread stands in for the audio thread by driving update(). Run under TSan/ASan
// (the yse_tests_tsan / _asan targets) they assert the race is gone.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "midi/midifile.hpp"
#include "midi/midifileManager.h"
#include "support/null_device.hpp"

namespace {

  // Stand in for the audio callback: drive MIDI::Manager().update() a handful of
  // times so the inbox drains, orphans are retired, and the slow-pool deleteJob
  // gets enqueued and reaps freed impls.
  void drainMidi(int iterations = 8, int sleepMs = 2) {
    for (int i = 0; i < iterations; ++i) {
      YSE::MIDI::Manager().update();
      if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  }

} // namespace

TEST_SUITE("midi") {

  // ─── Single-thread churn ─────────────────────────────────────────────────────

  TEST_CASE("midifile concurrency: single-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
      YSE::MIDI::file f;
      f.create("churn.mid");
      if ((i & 0x0f) == 0) drainMidi(2);
    }

    drainMidi(40);
    CHECK(true);
  }

  // ─── Two-thread churn ────────────────────────────────────────────────────────

  TEST_CASE("midifile concurrency: two-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    std::atomic<bool> workerDone{false};
    constexpr int N = 100;

    // Worker plays the main-thread role: it constructs/destroys file objects,
    // which emplace_front into `implementations` and null `head`.
    std::thread worker([&]() {
      for (int i = 0; i < N; ++i) {
        YSE::MIDI::file f;
        f.create("churn.mid");
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
      workerDone.store(true, std::memory_order_release);
    });

    // Test thread plays the audio-thread role: it drains the inbox and retires
    // orphans while the worker churns.
    int safety = 5000;
    while (!workerDone.load(std::memory_order_acquire) && --safety > 0) {
      drainMidi(2, 0);
    }
    worker.join();

    drainMidi(40);
    CHECK(true);
  }

} // TEST_SUITE("midi")
