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
#include "../implementations/logImplementation.h"
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
} // namespace

YSE::INTERNAL::threadPoolJob::threadPoolJob()
  : shouldStop(false), inQueue(false), isDone(false), pool(nullptr) {}

YSE::INTERNAL::threadPoolJob::~threadPoolJob() {
  join();
}

void YSE::INTERNAL::threadPoolJob::join() {
  // Help-running wait, called from the audio callback (buffersToParent) — it
  // must never sleep or lock. The old implementation slept in 2 ms quanta,
  // burning ~70% of a 2.9 ms render block waiting on a worker (issue #188);
  // its spin-only successor still made the callback's completion depend on
  // another thread's progress (issue #284). Instead, while this job isn't
  // finished, pop and run sibling jobs from the same render ring on this
  // thread: "wait for a possibly-preempted worker" becomes "do the remaining
  // render work yourself". The joined job itself may be the one popped, in
  // which case it simply runs inline here. Only when the ring is empty (the
  // job is mid-run on a worker) do we fall back to spinning, relaxing the CPU
  // for a while and then yielding so a not-yet-scheduled worker can run.
  // Render workers run at raised priority, so with helping the yield path is
  // effectively unreachable — even when priority elevation was denied by the
  // OS (see threadPool::startup), the callback keeps making progress instead
  // of burning its budget on a preempted worker.
  //
  // Background-pool jobs never help (their `pool` stays nullptr — see
  // threadPool::addJob): running disk-touching work on the joining thread
  // would be wrong; they use the pure spin/yield path, and are never joined
  // from the audio callback.
  //
  // Wait on inQueue, not isDone (issue #239). activate() (and shutdown()) write
  // inQueue=false *after* isDone=true, so inQueue is the worker's genuinely last
  // store into this job. Spinning on isDone let join() return in the window
  // between those two stores; the owner could then destroy the job while the
  // worker still had inQueue=false pending, landing that store in freed (or
  // reused) memory — a teardown use-after-free the TSan CI caught on the patcher
  // suite. Waiting on inQueue guarantees the worker is completely finished with
  // this job's memory before join() returns. A job that was never queued has
  // inQueue==false and returns immediately.
  constexpr int YIELD_AFTER = 8192;
  int spins = 0;
  while (inQueue) {
    threadPool* p = pool.load(std::memory_order_relaxed);
    if (p != nullptr && p->tryRunOne()) {
      // Made progress on the render pass; re-check immediately and restart
      // the backoff ladder.
      spins = 0;
      continue;
    }
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
  // No live caller stops a queued job today, but the trap is latent. Preserve
  // the #239 store order: isDone must land before inQueue so a racing join()'s
  // trailing store can't land in freed memory.
  if (!shouldStop && !isDone) run();
  isDone = true;
  inQueue = false;
}

YSE::INTERNAL::threadPoolThread::threadPoolThread(threadPool* pool) : pool(pool) {}

void YSE::INTERNAL::threadPoolThread::run() {
  // Worker threads also run DSP (CHANNEL::implementationObject::run dispatches
  // child-channel dsp() here via addFastJob). MXCSR/FPCR is per-thread, so the
  // FTZ/DAZ set on the audio callback thread does not reach us — set it once
  // when the worker starts. See issue #81 and denormalGuard.h.
  enableFlushToZero();

  while (!threadShouldExit()) {
    threadPoolJob* job = pool->getJob();
    if (job == nullptr) return;
    job->activate();
  }
}

YSE::INTERNAL::threadPool::threadPool(Int numThreads, poolClass cls)
  : jobs(cls == poolClass::render ? RENDER_CAPACITY : BACKGROUND_CAPACITY),
    poolSize(numThreads),
    classOf(cls),
    active(false) {
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
    // threads can't preempt one while the callback waits in join() (issue
    // #188). Best-effort — a denied request just leaves the worker at default
    // priority (join()'s help-running keeps the callback making progress
    // regardless), but log the denial once per process so deployments know
    // they run in the degraded mode (issue #284): without elevation, a
    // CPU-saturated box can preempt a worker mid-job and render latency
    // depends on the OS scheduler. Typical on Linux/Android without rtprio
    // privileges. Background workers stay at normal priority.
    if (classOf == poolClass::render && !threads.front().setPriority(true)) {
      static aBool priorityDenialLogged{false};
      if (!priorityDenialLogged.exchange(true)) {
        LogImpl().emit(E_WARNING, "render worker priority elevation denied by the OS; running "
                                  "at default priority (degraded real-time scheduling)");
      }
    }
  }
}

void YSE::INTERNAL::threadPool::shutdown() {
  if (!active) return; // already shut down — nothing to join or drain
  active = false;

  // Release any queued jobs so a pending join() returns instead of spinning
  // forever. Marking inQueue=false is enough (join() exits on !inQueue); we do
  // not run them. No lock: getJob() stops popping once active is false, so the
  // only other possible consumer is a still-spinning join() help-running via
  // tryRunOne() (issue #284) — the MPMC ring handles that concurrency, and a
  // job it steals simply runs instead of being released here. Either way every
  // queued job ends with inQueue == false.
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

  // Publish which ring this job lives on before it becomes joinable, so
  // join() can help-run sibling jobs while it waits (issue #284). Render
  // pools only: a background job must never pull disk-touching work onto the
  // thread that joins it. The store is ordered before start()'s inQueue=true
  // (seq_cst), so any join() that observes the job as queued also sees the
  // pool pointer.
  job->pool.store(classOf == poolClass::render ? this : nullptr, std::memory_order_relaxed);
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

bool YSE::INTERNAL::threadPool::tryRunOne() {
  // One non-blocking pop + inline run, for join()'s help-running (issue
  // #284). No allocation, no lock, no syscall — RT-safe on the audio
  // callback, exactly like the ring-full inline fallback in addJob(). No
  // `active` check: a job popped here would otherwise have been released
  // unrun by shutdown()'s drain, and running it is just as correct.
  threadPoolJob* job = nullptr;
  if (!jobs.try_pop(job)) return false;
  job->activate();
  return true;
}

YSE::INTERNAL::threadPoolJob* YSE::INTERNAL::threadPool::getJob() {
  // Adaptive backoff: pick up work within nanoseconds while the pool is hot
  // (busy render), fall to a cooperative yield, and only start sleeping once
  // the pool has been idle for a while so an idle engine doesn't peg a core.
  // No producer-side wakeup is needed, so addJob() never has to lock or notify.
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
