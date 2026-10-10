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

YSE::INTERNAL::threadPoolJob::threadPoolJob() : shouldStop(false), inQueue(false), isDone(false) {}

YSE::INTERNAL::threadPoolJob::~threadPoolJob() {
  join();
}

void YSE::INTERNAL::threadPoolJob::join() {
  // Wait on inQueue, not isDone (issue #239). activate() (and shutdown()) write
  // inQueue=false *after* isDone=true, so inQueue is the worker's genuinely last
  // store into this job. Spinning on isDone let join() return in the window
  // between those two stores; the owner could then destroy the job while the
  // worker still had inQueue=false pending, landing that store in freed (or
  // reused) memory — a teardown use-after-free the TSan CI caught on the patcher
  // suite. Waiting on inQueue guarantees the worker is completely finished with
  // this job's memory before join() returns. A job that was never queued has
  // inQueue==false and returns immediately.
  //
  // Background jobs are joined from control threads and teardown paths, never
  // from the audio callback, so the wait may yield. (The render path's
  // help-running join() of issue #284 left with the channel fan-out; rendering
  // runs on the renderScheduler, issue #859.)
  constexpr int YIELD_AFTER = 8192;
  int spins = 0;
  while (inQueue) {
    if (spins < YIELD_AFTER) {
      cpuRelax();
      ++spins;
    } else {
      std::this_thread::yield();
    }
  }
}

void YSE::INTERNAL::threadPoolJob::activate() {
  // Only run when the job is live, but ALWAYS clear the queued flags on the way
  // out — even on the stopped/already-done early-out. join() waits on inQueue
  // (issue #239), so an activate() that returned without clearing it would leave
  // a stopped-but-queued job spinning any future join() forever (issue #285).
  // Preserve the #239 store order: isDone must land before inQueue so a racing
  // join()'s trailing store can't land in freed memory.
  if (!shouldStop && !isDone) run();
  isDone = true;
  inQueue = false;
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
  // forever. Marking inQueue=false is enough (join() exits on !inQueue); we do
  // not run them. getJob() stops popping once active is false.
  threadPoolJob* job = nullptr;
  while (jobs.try_pop(job)) {
    // inQueue must be the last store, matching activate() and join()'s wait
    // flag (issue #239): a join() racing this drain returns only once inQueue is
    // clear, so isDone must land first or the trailing store lands in freed
    // memory.
    job->isDone = true;
    job->inQueue = false;
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
  // work touches disk): clear the queued flag and drop it; the manager/refill
  // schedulers are self-healing and re-enqueue next tick. Effectively
  // unreachable at the chosen capacity.
  assert(false && "background threadPool ring overflow");
  job->inQueue = false;
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
