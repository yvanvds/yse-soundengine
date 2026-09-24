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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_MSC_VER)
#pragma comment(lib, "synchronization.lib") // WaitOnAddress / WakeByAddress*
#endif
#define YSE_PARK_WAITONADDRESS 1
#elif defined(__linux__) // includes Android
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <climits>
#define YSE_PARK_FUTEX 1
#endif

namespace {
  // Park/wake primitives for render workers (issue #858). A parked worker
  // blocks in the kernel until the 32-bit word changes from `expected` (or a
  // spurious wakeup — callers re-check). The wake side is a single
  // non-blocking syscall: no user-space lock, no allocation.
  //
  // Platforms without an address-wait primitive (no current build target;
  // kept so the pool still compiles elsewhere) fall back to the old deep-idle
  // sleep: parkOn() naps 1 ms and wakeParked() is a no-op, so a "parked"
  // worker polls at 1 kHz.
  inline void parkOn(std::atomic<std::uint32_t>& word, std::uint32_t expected) {
#if defined(YSE_PARK_WAITONADDRESS)
    // std::atomic<uint32_t> is a lock-free 4-byte object with the same
    // representation as uint32_t on every supported compiler; WaitOnAddress
    // compares the raw bytes.
    WaitOnAddress(reinterpret_cast<volatile VOID*>(&word), &expected, sizeof(expected), INFINITE);
#elif defined(YSE_PARK_FUTEX)
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAIT_PRIVATE, expected,
            nullptr, nullptr, 0);
#else
    (void)word;
    (void)expected;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  }

  // Whether wakeParked() can wake exactly `count` waiters in one call. futex
  // can; Windows only has "one" or "all". Where it can't, wake() wakes a
  // single worker and each woken worker that finds a job passes the wake on
  // to one more (see threadPool::getRenderJob) — so the audio callback still
  // pays exactly one wake call per fan-out, and a 2-job fan-out does not
  // stampede every parked worker onto the CPUs the rendering thread needs.
#if defined(YSE_PARK_FUTEX)
  constexpr bool WAKE_COUNT_IS_EXACT = true;
#else
  constexpr bool WAKE_COUNT_IS_EXACT = false;
#endif

  // Wake up to `count` workers parked on `word`; count < 0 wakes all.
  inline void wakeParked(std::atomic<std::uint32_t>& word, int count) {
#if defined(YSE_PARK_WAITONADDRESS)
    if (count == 1)
      WakeByAddressSingle(reinterpret_cast<PVOID>(&word));
    else
      WakeByAddressAll(reinterpret_cast<PVOID>(&word));
#elif defined(YSE_PARK_FUTEX)
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word), FUTEX_WAKE_PRIVATE,
            count < 0 ? INT_MAX : count, nullptr, nullptr, 0);
#else
    (void)word;
    (void)count;
#endif
  }

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

namespace {
  // Resolve a requested worker count: -1 auto-sizes, anything else is honoured
  // as given, and the result is never below `floor`.
  Int resolvePoolSize(Int requested, YSE::INTERNAL::poolClass cls, Int floor) {
    Int size = requested;
    if (size == -1) {
      size = (Int)std::thread::hardware_concurrency();
      // Render fan-out does not scale with core count — it scales *against* it.
      // See MAX_AUTO_RENDER_THREADS in threadPool.h for the measurements (#650).
      // Background pools are sized explicitly by their owner, so this only ever
      // caps a render pool that asked to be auto-sized.
      if (cls == YSE::INTERNAL::poolClass::render &&
          size > YSE::INTERNAL::threadPool::MAX_AUTO_RENDER_THREADS) {
        size = YSE::INTERNAL::threadPool::MAX_AUTO_RENDER_THREADS;
      }
      // hardware_concurrency() may be 0 when it is not computable; auto-sizing
      // always yields at least one worker.
      if (size <= 0) size = 1;
    }
    if (size < floor) size = floor;
    return size;
  }
} // namespace

YSE::INTERNAL::threadPool::threadPool(Int numThreads, poolClass cls)
  : jobs(cls == poolClass::render ? RENDER_CAPACITY : BACKGROUND_CAPACITY),
    poolSize(resolvePoolSize(numThreads, cls, 1)),
    classOf(cls),
    active(false) {
  startup();
}

void YSE::INTERNAL::threadPool::setWorkerCount(Int numThreads) {
  const bool wasActive = active;
  shutdown();
  // Zero workers is only meaningful for a render pool, whose jobs are always
  // joined — and so help-run — by the dispatching thread. A background job is
  // fire-and-forget and would never run.
  poolSize = resolvePoolSize(numThreads, classOf, classOf == poolClass::render ? 0 : 1);
  if (wasActive) startup();
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

  // Unpark every render worker so it observes !active and exits (issue #858).
  // The epoch bump is ordered after the `active` store: a worker that read
  // active == true sampled the epoch before this bump, so its park either
  // fails the compare or is woken here. Unconditional (not gated on
  // `parked`): shutdown is a control-thread path, not RT.
  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  wakeParked(wakeEpoch, -1);

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

void YSE::INTERNAL::threadPool::wake(Int jobCount) {
  // Called on the audio callback once per fan-out (issue #858): must stay a
  // single non-blocking signal. Background workers never park.
  if (jobCount <= 0 || classOf != poolClass::render) return;

  // Store-load barrier pairing with the one in getRenderJob(): the caller's
  // ring pushes (stores) must be globally visible before we read `parked`,
  // and a parking worker's `parked` increment before it re-checks the ring.
  // With a fence on both sides at least one side sees the other: either we
  // see the worker announced and wake it, or it sees our job and never parks.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const Int sleeping = parked.load(std::memory_order_relaxed);
  if (sleeping == 0) return; // everyone is awake or spinning: no syscall

  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  // Wake no more workers than there are jobs: a woken worker that finds the
  // ring empty just re-parks, but there is no point paying for it.
  //
  // Without an exact wake count (Windows): when the fan-out needs every
  // parked worker, wake them all at once — in parallel, so no worker's start
  // waits on another's wake latency. When it needs fewer, wake one and let
  // each woken worker that finds a job pass the wake on (passWakeOn()), so a
  // small fan-out does not stampede a large pool onto the CPUs the rendering
  // thread needs. Measured on the #857 heavy scene: all-wake cost 10% at
  // W = 24 on 8 CPUs, one-and-chain cost 7-10% at W = 4 unpinned (the wake
  // latency serialises along the chain); this split takes the better of each.
  const Int wanted = jobCount < sleeping ? jobCount : sleeping;
  if (WAKE_COUNT_IS_EXACT)
    wakeParked(wakeEpoch, (int)wanted);
  else
    wakeParked(wakeEpoch, wanted >= sleeping ? -1 : 1);
}

void YSE::INTERNAL::threadPool::passWakeOn() {
  // A worker woken by wake() found a job: hand the wake on to one more parked
  // worker, in case the fan-out has more (issue #858). Only on platforms whose
  // wake cannot target a count (Windows); futex wakes the right number up
  // front. The chain ends at the first woken worker that finds the ring empty,
  // so a fan-out of n jobs wakes at most n + 1 workers — and this runs on a
  // worker, so the audio callback still made only its one wake call.
  if (WAKE_COUNT_IS_EXACT) return;
  // (A two-way fan-out per hop was measured too: 6.4 vs 5.0 ms per 16 blocks
  // at W = 24 on 8 CPUs — the overshoot stampedes the pool again.)
  if (parked.load(std::memory_order_relaxed) == 0) return;
  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  wakeParked(wakeEpoch, 1);
}

YSE::INTERNAL::threadPoolJob* YSE::INTERNAL::threadPool::getJob() {
  return classOf == poolClass::render ? getRenderJob() : getBackgroundJob();
}

bool YSE::INTERNAL::threadPool::spinForJob(threadPoolJob*& job) {
  // The bounded pre-park spin window (issue #858; see getRenderJob()).
  using clock = std::chrono::steady_clock;
  constexpr auto SPIN_WINDOW = std::chrono::microseconds(50);
  constexpr auto PAUSE_WINDOW = std::chrono::microseconds(10);
  const auto spinStart = clock::now();
  for (auto spun = clock::duration::zero(); active && spun < SPIN_WINDOW;
       spun = clock::now() - spinStart) {
    if (spun < PAUSE_WINDOW)
      cpuRelax();
    else
      std::this_thread::yield();
    if (jobs.try_pop(job)) return true;
  }
  return false;
}

YSE::INTERNAL::threadPoolJob* YSE::INTERNAL::threadPool::getRenderJob() {
  // Spin, then park (issue #858). The old strategy yield-spun for 5 ms before
  // sleeping; blocks arrive every ~2.9 ms live, so every idle render worker —
  // a raised-priority thread — sat in a yield() loop for the whole gap
  // between blocks, competing with the threads doing real work (including
  // their SMT siblings). Now an idle worker spins only a short window, long
  // enough to catch the next job of a fan-out already in flight, and then
  // parks in the kernel until wake() — which the dispatcher calls once per
  // fan-out — or shutdown(). A parked worker costs no CPU, so a paused or
  // idle engine's render pool sits at zero load. A worker that wakes late
  // just finds fewer (or no) jobs: the joining thread help-runs the rest.
  //
  // The spin window is PAUSE for its first 10 us, then yield() for the rest.
  // The yield tail is bounded (it ends in a park, never in another window),
  // and it is what keeps an oversubscribed pool honest: measured on the #857
  // heavy scene pinned to 8 logical CPUs, 8-24 raised-priority workers
  // PAUSE-spinning a full 50 us starved the (normal-priority) rendering
  // thread of a CPU — 0.31 / 0.34 ms per block at W = 8 / 24 vs 0.28 / 0.33
  // with the yield tail. Shorter pure-PAUSE windows parked too eagerly for
  // back-to-back offline blocks.
  threadPoolJob* job = nullptr;
  bool woken = false; // just returned from parkOn()

  while (active) {
    if (jobs.try_pop(job)) {
      if (woken) passWakeOn();
      return job;
    }

    // Spin only after running a job, where the next job of a fan-out still in
    // flight may be moments away. A worker just woken to an empty ring (the
    // jobs were taken by faster threads, or the wake raced a shutdown) goes
    // straight back to sleep: a raised-priority thread spinning on nothing is
    // exactly the CPU the rendering thread and the busy workers need.
    if (!woken && spinForJob(job)) return job;

    // Park. Sample the epoch first, then announce, then re-check the ring and
    // `active` — see wake() and shutdown() for the pairing arguments. Any
    // wake or shutdown after the sample changes the epoch, so parkOn()
    // returns immediately instead of sleeping through it.
    const std::uint32_t epoch = wakeEpoch.load(std::memory_order_seq_cst);
    parked.fetch_add(1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!active) {
      parked.fetch_sub(1, std::memory_order_seq_cst);
      break;
    }
    if (jobs.try_pop(job)) {
      parked.fetch_sub(1, std::memory_order_seq_cst);
      return job;
    }
    parkOn(wakeEpoch, epoch);
    parked.fetch_sub(1, std::memory_order_seq_cst);
    woken = true;
  }
  return nullptr;
}

YSE::INTERNAL::threadPoolJob* YSE::INTERNAL::threadPool::getBackgroundJob() {
  // Adaptive backoff: pick up work within nanoseconds while the pool is hot,
  // fall to a cooperative yield, and only start sleeping once the pool has
  // been idle for a while so an idle engine doesn't peg a core. No
  // producer-side wakeup is needed, so addJob() never has to lock or notify.
  // Background workers run at normal priority and see a handful of jobs per
  // manager tick, so the render pool's park/wake (issue #858) buys nothing
  // here.
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
