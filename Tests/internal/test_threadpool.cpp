// Tests for YSE::INTERNAL::threadPool — the wait-free job pool the audio
// callback dispatches DSP fan-out and manager jobs through (issue #188).
//
// These drive the pool directly (no engine session): they verify that jobs
// dispatched via addJob() actually run, that join() returns without the old
// 2 ms sleep, that a full render ring falls back to inline execution instead
// of dropping work, and that a pool survives shutdown()/startup() cycling.

#include <doctest/doctest.h>
#include "internal/threadPool.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

using YSE::INTERNAL::threadPool;
using YSE::INTERNAL::threadPoolJob;
using YSE::INTERNAL::poolClass;

namespace {
  // A job that records the thread it ran on and bumps a shared counter.
  struct CountJob : threadPoolJob {
    std::atomic<int>* counter;
    std::atomic<std::thread::id>* ranOn;
    explicit CountJob(std::atomic<int>* c, std::atomic<std::thread::id>* r = nullptr)
      : counter(c), ranOn(r) {}
    void run() override {
      if (ranOn) ranOn->store(std::this_thread::get_id());
      counter->fetch_add(1, std::memory_order_relaxed);
    }
  };
}

TEST_SUITE("internal") {

TEST_CASE("threadPool: every dispatched render job runs and joins") {
    threadPool pool(4, poolClass::render);
    std::atomic<int> counter{0};

    constexpr int N = 500;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i) jobs.emplace_back(std::make_unique<CountJob>(&counter));

    for (auto& j : jobs) pool.addJob(j.get());
    for (auto& j : jobs) j->join();

    CHECK(counter.load() == N);
    for (auto& j : jobs) CHECK(j->isQueued() == false);
}

TEST_CASE("threadPool: background pool runs fire-and-forget jobs") {
    threadPool pool(1, poolClass::background);
    std::atomic<int> counter{0};

    constexpr int N = 100;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i) jobs.emplace_back(std::make_unique<CountJob>(&counter));

    for (auto& j : jobs) pool.addJob(j.get());
    for (auto& j : jobs) j->join();

    CHECK(counter.load() == N);
}

TEST_CASE("threadPool: join() returns promptly (no 2 ms sleep quantum)") {
    threadPool pool(4, poolClass::render);
    std::atomic<int> counter{0};

    // Dispatch a batch and time the round-trip. The old join() slept in 2 ms
    // steps, so N joins on completed-but-not-yet-observed jobs cost O(N * 2ms).
    // The spin join should clear this in well under that.
    constexpr int N = 50;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i) jobs.emplace_back(std::make_unique<CountJob>(&counter));

    auto t0 = std::chrono::steady_clock::now();
    for (auto& j : jobs) pool.addJob(j.get());
    for (auto& j : jobs) j->join();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK(counter.load() == N);
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 50);
}

TEST_CASE("threadPool: render ring overflow falls back to inline execution") {
    // RENDER_CAPACITY is 4096. Stalling all workers and then dispatching more
    // than the ring holds forces the inline fallback path: no job is dropped.
    threadPool pool(2, poolClass::render);
    std::atomic<int> counter{0};

    // A blocking job pins a worker until released, so the ring can actually
    // fill instead of being drained as fast as we push.
    struct BlockJob : threadPoolJob {
      std::atomic<bool>* release;
      explicit BlockJob(std::atomic<bool>* r) : release(r) {}
      void run() override {
        while (!release->load(std::memory_order_acquire)) std::this_thread::yield();
      }
    };

    std::atomic<bool> release{false};
    std::vector<std::unique_ptr<BlockJob>> blockers;
    for (int i = 0; i < 2; ++i) { // one per worker
      blockers.emplace_back(std::make_unique<BlockJob>(&release));
      pool.addJob(blockers.back().get());
    }

    // Now flood past capacity. With both workers stuck, ~4096 land in the ring
    // and the remainder run inline on this (calling) thread.
    constexpr int N = 5000;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i) {
      jobs.emplace_back(std::make_unique<CountJob>(&counter));
      pool.addJob(jobs.back().get());
    }

    release.store(true, std::memory_order_release);
    for (auto& b : blockers) b->join();
    for (auto& j : jobs) j->join();

    CHECK(counter.load() == N); // nothing dropped
}

TEST_CASE("threadPool: survives shutdown()/startup() cycling") {
    threadPool pool(2, poolClass::render);
    std::atomic<int> counter{0};

    auto runBatch = [&](int n) {
      std::vector<std::unique_ptr<CountJob>> jobs;
      jobs.reserve(n);
      for (int i = 0; i < n; ++i) jobs.emplace_back(std::make_unique<CountJob>(&counter));
      for (auto& j : jobs) pool.addJob(j.get());
      for (auto& j : jobs) j->join();
    };

    runBatch(50);
    pool.shutdown();
    // addJob on an inactive pool is a no-op; the job stays un-queued.
    CountJob ignored(&counter);
    pool.addJob(&ignored);
    CHECK(ignored.isQueued() == false);

    pool.startup();
    runBatch(50);

    CHECK(counter.load() == 100);
}

} // TEST_SUITE
