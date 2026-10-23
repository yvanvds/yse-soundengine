/*
  ==============================================================================

    threadPool.cpp
    Created: 1 Oct 2014 12:37:59pm
    Author:  yvan

  ==============================================================================
*/

#include "threadPool.h"
#include <assert.h>
#include <chrono>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {
  // Relax the CPU inside a spin loop: PAUSE on x86, YIELD on ARM, a portable
  // no-op elsewhere. Not a scheduling yield.
  inline void cpuRelax() {
#if defined(_MSC_VER)
    _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    std::this_thread::yield();
#endif
  }
} // namespace

YSE::INTERNAL::threadPoolJob::threadPoolJob() : shouldStop(false), queued(0) {}

YSE::INTERNAL::threadPoolJob::~threadPoolJob() {
  join();
}

void YSE::INTERNAL::threadPoolJob::join() {
  // Wait for the queued count to reach zero. The worker's subtraction in
  // release() is its genuinely last store into this job (issue #239: an earlier
  // join() returned before the worker's final store, which then landed in freed
  // memory). A job that was never queued has a zero count and returns
  // immediately. A start() made while run() is executing keeps the count above
  // zero, so join() also waits for the extra run that start() asked for
  // (issue #1190).
  //
  // Background jobs are joined from control threads and teardown paths, never
  // from the audio callback, so the wait may yield. (The render path's
  // help-running join() of issue #284 left with the channel fan-out; rendering
  // runs on the renderScheduler, issue #859.)
  constexpr int YIELD_AFTER = 8192;
  int spins = 0;
  while (isQueued()) {
    if (spins < YIELD_AFTER) {
      cpuRelax();
      ++spins;
    } else {
      std::this_thread::yield();
    }
  }
}

void YSE::INTERNAL::threadPoolJob::release(std::uint32_t n) {
  // A plain fetch_sub could wrap: shutdown() zeroes the count of a job it
  // drains from the ring even if a worker is running that job right now, and
  // that worker then subtracts its own snapshot. Clamp at zero instead.
  // Lock-free; only contended by a concurrent start() or shutdown().
  std::uint32_t cur = queued.load();
  while (!queued.compare_exchange_weak(cur, cur > n ? cur - n : 0)) {}
}

void YSE::INTERNAL::threadPoolJob::activate() {
  // Snapshot the outstanding start()s, run once for all of them, then subtract
  // exactly that snapshot (issue #1190). A start() that lands while run() is
  // executing is not in the snapshot, so it leaves the count above zero: its
  // own ring entry is popped next and runs run() again, and join()/isQueued()
  // keep reporting the job as queued until it has. The old inQueue/isDone
  // epilogue overwrote such a start() instead, and the re-pushed entry was then
  // skipped as already done.
  //
  // A snapshot of zero means an earlier activate() already ran for this entry
  // (the job was pushed twice before a worker reached it), so there is nothing
  // to do. A stopped job is not run, but the count is still released on the way
  // out, or a later join() would spin forever (issue #285).
  const std::uint32_t n = queued.load();
  if (n == 0) return;
  if (!shouldStop) run();
  // Last store into this job: once the count reaches zero, join() may return and
  // the owner may destroy it (issue #239).
  release(n);
}

YSE::INTERNAL::threadPoolThread::threadPoolThread(threadPool* pool) : pool(pool) {}

void YSE::INTERNAL::threadPoolThread::run() {
  while (!threadShouldExit()) {
    threadPoolJob* job = pool->getJob();
    if (job == nullptr) return;
    job->activate();
  }
}

YSE::INTERNAL::threadPool::threadPool(Int numThreads)
  : jobs(CAPACITY), poolSize(numThreads < 1 ? 1 : numThreads), active(false) {
  startup();
}

YSE::INTERNAL::threadPool::~threadPool() {
  shutdown();
}

void YSE::INTERNAL::threadPool::startup() {
  if (active) return; // already running — startup() is idempotent
  active = true;
  for (Int i = 0; i < poolSize; i++) {
    threads.emplace_front(this);
    threads.front().start();
  }
}

void YSE::INTERNAL::threadPool::shutdown() {
  if (!active) return; // already shut down — nothing to join or drain
  active = false;

  // Release any queued jobs so a pending join() returns instead of spinning
  // forever. Zeroing the queued count is enough (join() exits on zero); we do
  // not run them. getJob() stops popping once active is false. The store is the
  // last one this drain makes into the job (issue #239).
  threadPoolJob* job = nullptr;
  while (jobs.try_pop(job)) {
    job->queued.store(0);
  }

  for (auto i = threads.begin(); i != threads.end(); ++i) {
    i->stop();
  }
  // Drop the now-joined worker objects; startup() re-populates the list when
  // the pool is revived for the next session (issue #140). Clearing also lets
  // ~threadPoolThread's assert(!isRunning()) pass — every handle is null now.
  threads.clear();
}

void YSE::INTERNAL::threadPool::addJob(threadPoolJob* job) {
  if (!active) return;

  job->start();
  if (jobs.try_push(job)) return;

  // Ring full. Never run inline (the caller may be the audio thread and the
  // work touches disk): take back this push's count and drop it; the
  // manager/refill schedulers are self-healing and re-enqueue next tick.
  // Effectively unreachable at the chosen capacity.
  assert(false && "background threadPool ring overflow");
  job->release(1);
}

YSE::INTERNAL::threadPoolJob* YSE::INTERNAL::threadPool::getJob() {
  // Adaptive backoff: pick up work within nanoseconds while the pool is hot,
  // fall to a cooperative yield, and only start sleeping once the pool has
  // been idle for a while so an idle engine doesn't peg a core. No
  // producer-side wakeup is needed, so addJob() never has to lock or notify.
  using clock = std::chrono::steady_clock;
  auto idleStart = clock::now();
  threadPoolJob* job = nullptr;

  while (active) {
    if (jobs.try_pop(job)) return job;

    auto idle = clock::now() - idleStart;
    if (idle < std::chrono::microseconds(50)) {
      cpuRelax();
    } else if (idle < std::chrono::milliseconds(5)) {
      std::this_thread::yield();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return nullptr;
}
