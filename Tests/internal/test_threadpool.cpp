// Tests for YSE::INTERNAL::threadPool — the wait-free job pool the audio
// callback dispatches DSP fan-out and manager jobs through (issue #188).
//
// These drive the pool directly (no engine session): they verify that jobs
// dispatched via addJob() actually run, that join() returns without the old
// 2 ms sleep, that a full render ring falls back to inline execution instead
// of dropping work, and that a pool survives shutdown()/startup() cycling.

#include <doctest/doctest.h>
#include "internal/threadPool.h"
#include "internal/thread.h"
#include <atomic>
#include <chrono>
#include <new>
#include <thread>
#include <vector>
#include <memory>

using YSE::INTERNAL::poolClass;
using YSE::INTERNAL::threadPool;
using YSE::INTERNAL::threadPoolJob;

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

  // A job that signals when it starts running and then pins its worker thread
  // until released.
  struct BlockJob : threadPoolJob {
    std::atomic<bool>* release;
    std::atomic<bool>* started;
    explicit BlockJob(std::atomic<bool>* r, std::atomic<bool>* s = nullptr)
      : release(r), started(s) {}
    void run() override {
      if (started) started->store(true, std::memory_order_release);
      while (!release->load(std::memory_order_acquire))
        std::this_thread::yield();
    }
  };
} // namespace

TEST_SUITE("internal") {

  TEST_CASE("threadPool: every dispatched render job runs and joins") {
    threadPool pool(4, poolClass::render);
    std::atomic<int> counter{0};

    constexpr int N = 500;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i)
      jobs.emplace_back(std::make_unique<CountJob>(&counter));

    for (auto& j : jobs)
      pool.addJob(j.get());
    for (auto& j : jobs)
      j->join();

    CHECK(counter.load() == N);
    for (auto& j : jobs)
      CHECK(j->isQueued() == false);
  }

  TEST_CASE("threadPool: background pool runs fire-and-forget jobs") {
    threadPool pool(1, poolClass::background);
    std::atomic<int> counter{0};

    constexpr int N = 100;
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i)
      jobs.emplace_back(std::make_unique<CountJob>(&counter));

    for (auto& j : jobs)
      pool.addJob(j.get());
    for (auto& j : jobs)
      j->join();

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
    for (int i = 0; i < N; ++i)
      jobs.emplace_back(std::make_unique<CountJob>(&counter));

    auto t0 = std::chrono::steady_clock::now();
    for (auto& j : jobs)
      pool.addJob(j.get());
    for (auto& j : jobs)
      j->join();
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
    for (auto& b : blockers)
      b->join();
    for (auto& j : jobs)
      j->join();

    CHECK(counter.load() == N); // nothing dropped
  }

  TEST_CASE("threadPool: join() waits for the worker's final store (no teardown race)") {
    // Regression for issue #239. activate() publishes completion as
    // `isDone=true; inQueue=false;`, so inQueue is the worker's genuinely last
    // store into the job. The old join() spun on isDone and returned in the
    // window before that trailing inQueue store, letting the owner destroy the
    // job while the worker still had a store pending — a teardown
    // use-after-free the TSan CI caught on the patcher suite.
    //
    // Here we reuse a single storage slot: construct a job, dispatch it, join,
    // destroy it, then immediately reconstruct another in the same bytes. If
    // join() returns too early the worker's trailing store races the destructor
    // and the placement-new that follows — exactly the "activate() inQueue store
    // vs threadPoolJob() ctor" race from the issue's TSan trace. Build under
    // TSan (build-linux-tsan) to observe the race; a plain build simply confirms
    // every job still runs. With join() waiting on inQueue the reuse is safe.
    threadPool pool(4, poolClass::render);
    std::atomic<int> counter{0};

    alignas(CountJob) unsigned char storage[sizeof(CountJob)];
    constexpr int N = 20000;
    for (int i = 0; i < N; ++i) {
      auto* job = new (storage) CountJob(&counter);
      pool.addJob(job);
      job->join();
      job->~CountJob();
    }

    CHECK(counter.load() == N);
  }

  TEST_CASE("threadPool: join() help-runs queued render jobs while it waits (issue #284)") {
    // Regression for issue #284. The spin-only join() made the audio
    // callback's completion depend on another thread's progress: with every
    // worker preempted (or otherwise stalled), join() on a still-queued job
    // spun/yielded until a worker came back. With help-running, join() pops
    // runnable jobs from the same render ring and runs them on the calling
    // thread, so it completes even when no worker makes any progress.
    //
    // Model the worst case exactly: a single-worker pool whose only worker is
    // pinned by a blocking job. Every job queued behind it can then only run
    // if join() helps. Without the fix this test never terminates.
    threadPool pool(1, poolClass::render);

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    BlockJob blocker(&release, &started);
    pool.addJob(&blocker);

    // Wait until the worker has actually popped the blocker, so the ring
    // holds only the jobs below and the worker is provably out of play.
    while (!started.load(std::memory_order_acquire))
      std::this_thread::yield();

    constexpr int N = 32;
    std::atomic<int> counter{0};
    std::vector<std::atomic<std::thread::id>> ranOn(N);
    std::vector<std::unique_ptr<CountJob>> jobs;
    jobs.reserve(N);
    for (int i = 0; i < N; ++i) {
      jobs.emplace_back(std::make_unique<CountJob>(&counter, &ranOn[i]));
      pool.addJob(jobs.back().get());
    }

    // The worker is pinned, so these joins can only return via help-running.
    for (auto& j : jobs)
      j->join();

    CHECK(counter.load() == N);
    // And the help must have happened on this (the joining) thread. Compare
    // outside CHECK: doctest would otherwise try to stringify the
    // std::thread::id operands, which fails to compile on libstdc++
    // (gcc 13/14 headers).
    for (int i = 0; i < N; ++i) {
      const bool ranOnJoiningThread = ranOn[i].load() == std::this_thread::get_id();
      CHECK(ranOnJoiningThread);
    }

    release.store(true, std::memory_order_release);
    blocker.join();
  }

  TEST_CASE("thread: setPriority reports whether the request took effect (issue #284)") {
    // setPriority used to swallow denial silently; threadPool::startup() now
    // logs the degraded mode, which needs an observable result. Elevation
    // (`high == true`) is genuinely platform/privilege dependent, so only its
    // invariants are asserted here: false before start(), and a plain
    // normal-priority request on a running thread must succeed everywhere.
    struct NapThread : YSE::INTERNAL::thread {
      void run() override {
        while (!threadShouldExit())
          std::this_thread::yield();
      }
    };

    NapThread t;
    CHECK(t.setPriority(true) == false); // no underlying handle yet
    t.start();
    CHECK(t.setPriority(false) == true); // normal priority is always grantable
    // Best-effort elevation: result depends on OS privileges, must not crash
    // and must leave the thread joinable either way.
    (void)t.setPriority(true);
    t.stop();
    CHECK(t.isRunning() == false);
  }

  TEST_CASE("threadPool: survives shutdown()/startup() cycling") {
    threadPool pool(2, poolClass::render);
    std::atomic<int> counter{0};

    auto runBatch = [&](int n) {
      std::vector<std::unique_ptr<CountJob>> jobs;
      jobs.reserve(n);
      for (int i = 0; i < n; ++i)
        jobs.emplace_back(std::make_unique<CountJob>(&counter));
      for (auto& j : jobs)
        pool.addJob(j.get());
      for (auto& j : jobs)
        j->join();
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
