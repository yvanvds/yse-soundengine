// Tests for YSE::INTERNAL::threadPool — the lock-free background job pool
// (manager setup/delete, file loading, stream refill). Rendering moved to the
// task-graph renderScheduler in issue #859 (see test_render_scheduler.cpp);
// the render-class pool behaviour these tests used to pin (help-running
// join(), inline overflow, park/wake) left with it.
//
// These drive the pool directly (no engine session): they verify that jobs
// dispatched via addJob() actually run, that join() waits for the worker's
// last store, and that a pool survives shutdown()/startup() cycling.

#include <doctest/doctest.h>
#include "internal/threadPool.h"
#include "internal/thread.h"
#include <atomic>
#include <chrono>
#include <new>
#include <thread>
#include <vector>
#include <memory>

using YSE::INTERNAL::threadPool;
using YSE::INTERNAL::threadPoolJob;

namespace {
  // A job that bumps a shared counter.
  struct CountJob : threadPoolJob {
    std::atomic<int>* counter;
    explicit CountJob(std::atomic<int>* c) : counter(c) {}
    void run() override {
      counter->fetch_add(1, std::memory_order_relaxed);
    }
  };
} // namespace

TEST_SUITE("internal") {

  TEST_CASE("threadPool: background pool runs fire-and-forget jobs") {
    threadPool pool(1);
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
    for (auto& j : jobs)
      CHECK(j->isQueued() == false);
  }

  TEST_CASE("threadPool: join() waits for the worker's final store (no teardown race)") {
    // Regression for issue #239. activate() published completion as
    // `isDone=true; inQueue=false;` (a single queued count since #1190, whose
    // release is the worker's last store). The old join() spun on isDone and returned in the
    // window before that trailing inQueue store, letting the owner destroy the
    // job while the worker still had a store pending — a teardown
    // use-after-free the TSan CI caught on the patcher suite.
    //
    // Reuse a single storage slot: construct a job, dispatch it, join, destroy
    // it, then immediately reconstruct another in the same bytes. If join()
    // returns too early the worker's trailing store races the destructor and
    // the placement-new that follows. Build under TSan to observe the race; a
    // plain build confirms every job still runs.
    threadPool pool(2);
    std::atomic<int> counter{0};

    alignas(CountJob) unsigned char storage[sizeof(CountJob)];
    constexpr int N = 5000;
    for (int i = 0; i < N; ++i) {
      auto* job = new (storage) CountJob(&counter);
      pool.addJob(job);
      job->join();
      job->~CountJob();
    }

    CHECK(counter.load() == N);
  }

  TEST_CASE("threadPool: activate() on a stopped queued job clears inQueue (issue #285)") {
    // Regression for issue #285. activate() used to early-return when
    // shouldStop was set WITHOUT clearing inQueue. join() waits on inQueue
    // (issue #239), so any caller that stops a still-queued job and then
    // joins or destroys it (~threadPoolJob calls join()) would spin forever.
    // activate() must clear inQueue on every exit while still not running a
    // stopped job.
    std::atomic<int> counter{0};
    CountJob job(&counter);

    job.start(); // inQueue = true, exactly as addJob() would set it
    CHECK(job.isQueued() == true);
    job.stop(); // a stop() caller marks it shouldStop
    job.activate(); // a worker picks it up

    CHECK(counter.load() == 0); // a stopped job must not run
    CHECK(job.isQueued() == false); // ...but must leave the queue flag cleared
    job.join(); // and so join() returns instead of spinning
  }

  TEST_CASE("threadPool: a start() made from inside run() runs the job again (issue #1190)") {
    // Driven by hand, the way the pool would: start() is addJob's half, each
    // activate() is one ring entry being popped. The first run() re-queues its
    // own job — what timerBridge::RunSlot's handoff lets a requester do while
    // the worker is still inside run(). The old activate() epilogue
    // (`isDone = true; inQueue = false;`) overwrote that start(): the job read as
    // idle and the second entry was skipped as already done.
    struct RequeueJob : threadPoolJob {
      int runs = 0;
      void run() override {
        if (++runs == 1) start();
      }
    };
    RequeueJob job;

    job.start();
    job.activate(); // first entry: run() re-queues
    CHECK(job.runs == 1);
    CHECK(job.isQueued() == true); // the re-push survived the epilogue

    job.activate(); // the re-pushed entry
    CHECK(job.runs == 2);
    CHECK(job.isQueued() == false);
    job.join();
  }

  TEST_CASE("threadPool: two start()s before a pop still run the job once (issue #1190)") {
    // The other half of the old isDone flag: a job pushed twice before a worker
    // reaches it runs once for both entries; the second entry is a no-op.
    std::atomic<int> counter{0};
    CountJob job(&counter);

    job.start();
    job.start();
    job.activate();
    CHECK(counter.load() == 1);
    CHECK(job.isQueued() == false);
    job.activate(); // the second ring entry
    CHECK(counter.load() == 1);
    CHECK(job.isQueued() == false);
  }

  TEST_CASE(
      "threadPool: a re-push while run() executes runs again and join() waits (issue #1190)") {
    // The same race on a live pool, made deterministic with latches: the first
    // run() parks until this thread has queued the job a second time, and the
    // second run() parks again so isQueued() can be checked while it is pending.
    // On the old flags the second entry was popped as already done, so the
    // second run() never started and the wait below timed out.
    struct LatchJob : threadPoolJob {
      std::atomic<int> entered{0};
      std::atomic<int> finished{0};
      std::atomic<int> release{0}; // how many runs may leave
      void run() override {
        const int n = entered.fetch_add(1) + 1;
        while (release.load() < n)
          std::this_thread::yield();
        finished.fetch_add(1);
      }
    };
    auto waitFor = [](const std::atomic<int>& v, int want) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (v.load() < want) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::yield();
      }
      return true;
    };

    threadPool pool(1);
    LatchJob job;

    pool.addJob(&job);
    REQUIRE(waitFor(job.entered, 1)); // the worker is inside run()
    pool.addJob(&job); // re-push from another thread mid-run
    job.release.store(1); // let the first run return through the epilogue

    // The second entry must run the job again, and until it has finished the
    // job still reads as queued.
    const bool rerun = waitFor(job.entered, 2);
    if (!rerun) job.release.store(2); // unblock teardown before failing
    REQUIRE(rerun);
    CHECK(job.finished.load() == 1);
    CHECK(job.isQueued() == true);

    job.release.store(2);
    job.join();
    CHECK(job.finished.load() == 2);
    CHECK(job.isQueued() == false);
  }

  TEST_CASE("thread: setPriority reports whether the request took effect (issue #284)") {
    // setPriority used to swallow denial silently; the render scheduler's
    // startup() logs the degraded mode, which needs an observable result.
    // Elevation (`high == true`) is genuinely platform/privilege dependent, so
    // only its invariants are asserted here: false before start(), and a plain
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
    threadPool pool(2);
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

  TEST_CASE("threadPool: a background pool always keeps a worker") {
    // Nothing else ever runs a background job, so a pool without workers
    // would silently drop work: the count is clamped to at least one.
    threadPool zero(0);
    CHECK(zero.workerCount() == 1);
    threadPool three(3);
    CHECK(three.workerCount() == 3);
  }

} // TEST_SUITE
