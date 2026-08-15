// A parking brake for the background pool, shared by the patcher tests that
// have to state what has *not* happened yet.
//
// Extracted from test_patcher_array_deserialize.cpp (issue #854) when
// test_patcher_dict_deserialize.cpp needed the same thing (issue #855) — one
// definition rather than a copy per parser family.

#pragma once

#include "internal/global.h"
#include "internal/threadPool.h"
#include "support/timer_pacing.hpp"

#include <atomic>
#include <thread>

namespace TestHelpers {

  // Parks the background pool's one worker: while this job is inside run(),
  // nothing else the pool has been handed can have started. That is what lets
  // a case state what has *not* happened yet without betting on how fast a
  // parse is — the deserialize family's busy rule (one slot, one document),
  // and its in-patcher case, whose "nothing announced yet" is only a fact
  // while the parse is provably unfinished.
  struct PoolBlocker : YSE::INTERNAL::threadPoolJob {
    std::atomic<bool> running{false};
    std::atomic<bool> release{false};

    // Releases on the way out as well, so a REQUIRE firing inside the parked
    // window unwinds instead of hanging: ~threadPoolJob joins.
    ~PoolBlocker() override {
      release.store(true, std::memory_order_release);
    }

    void run() override {
      running.store(true, std::memory_order_release);
      while (!release.load(std::memory_order_acquire))
        std::this_thread::yield();
    }

    // Queue it and wait until the worker is really inside run(): queued is not
    // parked. False if it never got there, so the caller REQUIREs it.
    bool Park() {
      YSE::INTERNAL::Global().addSlowJob(this);
      return TestHelpers::pacedUntil(5000,
                                     [this] { return running.load(std::memory_order_acquire); });
    }

    // Let the worker go and wait for it to leave run(), so whatever queued
    // behind it can start.
    void Unpark() {
      release.store(true, std::memory_order_release);
      join();
    }
  };

} // namespace TestHelpers
