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
#include "../system.hpp"
#include "denormalGuard.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {
  // Relax the CPU inside a spin loop: on x86 this is the PAUSE instruction
  // (frees the pipeline / hyperthread sibling and cuts power), on ARM the YIELD
  // hint, and a portable no-op elsewhere. Not a scheduling yield — the caller
  // decides when to escalate to std::this_thread::yield().
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
}

YSE::INTERNAL::threadPoolJob::threadPoolJob() : shouldStop(false), inQueue(false), isDone(false) {}

YSE::INTERNAL::threadPoolJob::~threadPoolJob() {
  join();
}

void YSE::INTERNAL::threadPoolJob::join() {
  // Bounded-spin wait, called from the audio callback (buffersToParent) — it
  // must never sleep or lock. The old implementation slept in 2 ms quanta,
  // burning ~70% of a 2.9 ms render block waiting on a worker (issue #188).
  // Instead spin on the done flag, relaxing the CPU for a while and then
  // yielding so a not-yet-scheduled worker can run. Render workers run at
  // raised priority, so the yield path is rarely reached.
  constexpr int YIELD_AFTER = 8192;
  int spins = 0;
  while (!isDone) {
    if (!inQueue) return; // never queued, or already drained by shutdown()
    if (spins < YIELD_AFTER) {
      cpuRelax();
      ++spins;
    } else {
      std::this_thread::yield();
    }
  }
}

void YSE::INTERNAL::threadPoolJob::activate() {
  if (shouldStop || isDone)  return;

  run();
  isDone = true;
  inQueue = false;
}

YSE::INTERNAL::threadPoolThread::threadPoolThread(threadPool * pool) : pool(pool) {}

void YSE::INTERNAL::threadPoolThread::run() {
  // Worker threads also run DSP (CHANNEL::implementationObject::run dispatches
  // child-channel dsp() here via addFastJob). MXCSR/FPCR is per-thread, so the
  // FTZ/DAZ set on the audio callback thread does not reach us — set it once
  // when the worker starts. See issue #81 and denormalGuard.h.
  enableFlushToZero();

  while (!threadShouldExit()) {
    threadPoolJob * job = pool->getJob();
    if (job == nullptr) return;
    job->activate();
  }
}

YSE::INTERNAL::threadPool::threadPool(Int numThreads, poolClass cls)
  : jobs(cls == poolClass::render ? RENDER_CAPACITY : BACKGROUND_CAPACITY),
    poolSize(numThreads), classOf(cls), active(false) {
  if (poolSize == -1) {
    poolSize = std::thread::hardware_concurrency();
  }

  // this might happen if hardware_concurrency() is not well defined or not computable
  // in which case we need at least one thread to continue
  if (poolSize == 0) {
    poolSize = 1;
  }

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
    // Render workers race the audio-callback deadline: raise them so ordinary
    // threads can't preempt one while the callback spins in join() (issue
    // #188). Best-effort — a denied request just leaves the worker at default
    // priority. Background workers stay at normal priority.
    if (classOf == poolClass::render) {
      threads.front().setPriority(true);
    }
  }
}

void YSE::INTERNAL::threadPool::shutdown() {
  if (!active) return; // already shut down — nothing to join or drain
  active = false;

  // Release any queued jobs so a pending join() returns instead of spinning
  // forever. Marking inQueue=false is enough (join() exits on !inQueue); we do
  // not run them. No lock: getJob() stops popping once active is false, so this
  // thread is the only consumer of the ring now.
  threadPoolJob * job = nullptr;
  while (jobs.try_pop(job)) {
    job->inQueue = false;
    job->isDone = true;
  }

  for (auto i = threads.begin(); i != threads.end(); ++i) {
    i->stop();
  }
  // Drop the now-joined worker objects; startup() re-populates the list when
  // the pool is revived for the next session (issue #140). Clearing also lets
  // ~threadPoolThread's assert(!isRunning()) pass — every handle is null now.
  threads.clear();
}

void YSE::INTERNAL::threadPool::addJob(threadPoolJob * job) {
  if (!active) return;

  job->start();
  if (jobs.try_push(job)) return;

  // Ring full. For a render pool the caller (audio thread or a worker) runs the
  // job inline so no DSP work is lost — correct, just serial for this one job.
  // A background pool must not run inline (the caller may be the audio thread
  // and the work touches disk), so we clear the queued flag and drop it; the
  // manager/refill schedulers are self-healing and re-enqueue next tick. Both
  // paths are effectively unreachable at the chosen capacities.
  if (classOf == poolClass::render) {
    job->activate();
  } else {
    assert(false && "background threadPool ring overflow");
    job->inQueue = false;
  }
}

YSE::INTERNAL::threadPoolJob * YSE::INTERNAL::threadPool::getJob() {
  // Adaptive backoff: pick up work within nanoseconds while the pool is hot
  // (busy render), fall to a cooperative yield, and only start sleeping once
  // the pool has been idle for a while so an idle engine doesn't peg a core.
  // No producer-side wakeup is needed, so addJob() never has to lock or notify.
  using clock = std::chrono::steady_clock;
  auto idleStart = clock::now();
  threadPoolJob * job = nullptr;

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
