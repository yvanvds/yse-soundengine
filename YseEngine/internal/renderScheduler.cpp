/*
  ==============================================================================

    renderScheduler.cpp
    Task-graph render scheduler (issue #859, epic #856).

  ==============================================================================
*/

#include "renderScheduler.h"
#include <chrono>
#include <thread>
#include "thread.h"
#include "denormalGuard.h"
#include "../implementations/logImplementation.h"

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
  // Park/wake primitives (moved here from the channel fan-out pool, issue
  // #858). A parked worker blocks in the kernel until the 32-bit word changes
  // from `expected` (or a spurious wakeup — callers re-check). The wake side
  // is a single non-blocking syscall: no user-space lock, no allocation.
  //
  // Platforms without an address-wait primitive (no current build target)
  // fall back to a 1 ms nap, so a "parked" worker polls at 1 kHz.
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
  // can; Windows only has "one" or "all" — there a partial wake wakes one
  // worker and every woken worker that finds work passes it on (passWakeOn).
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

  // Relax the CPU inside a spin loop: PAUSE on x86, YIELD on ARM. Not a
  // scheduling yield.
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

  Int resolveWorkerCount(Int requested) {
    if (requested >= 0) return requested;
    Int size = (Int)std::thread::hardware_concurrency();
    if (size > YSE::INTERNAL::renderScheduler::MAX_AUTO_WORKERS)
      size = YSE::INTERNAL::renderScheduler::MAX_AUTO_WORKERS;
    // hardware_concurrency() may be 0 when it is not computable; auto-sizing
    // always yields at least one worker.
    if (size <= 0) size = 1;
    return size;
  }
} // namespace

// One render worker thread. Its list index is fixed for its lifetime.
class YSE::INTERNAL::renderScheduler::worker : public YSE::INTERNAL::thread {
public:
  worker(renderScheduler& owner, Int index) : owner(owner), index(index) {}
  void run() override {
    owner.workerLoop(index);
  }

private:
  renderScheduler& owner;
  Int index;
};

YSE::INTERNAL::renderScheduler::renderScheduler(Int requested)
  : numWorkers(resolveWorkerCount(requested)),
    lists(std::make_unique<leafList[]>(static_cast<std::size_t>(numWorkers) + 1)) {
  startup();
}

YSE::INTERNAL::renderScheduler::~renderScheduler() {
  shutdown();
}

void YSE::INTERNAL::renderScheduler::setWorkerCount(Int requested) {
  const bool wasActive = active;
  shutdown();
  numWorkers = resolveWorkerCount(requested);
  lists = std::make_unique<leafList[]>(static_cast<std::size_t>(numWorkers) + 1);
  leafCount = 0;
  // Leaves are dealt out per worker: the graph must be rebuilt for the new
  // list count before the next block.
  markDirty();
  if (wasActive) startup();
}

void YSE::INTERNAL::renderScheduler::startup() {
  if (active) return;
  active = true;
  running.store(true, std::memory_order_seq_cst);
  workers.reserve(static_cast<std::size_t>(numWorkers));
  for (Int i = 1; i <= numWorkers; ++i) {
    workers.push_back(std::make_unique<worker>(*this, i));
    workers.back()->start();
    // A worker races the callback deadline: raise it so ordinary threads can't
    // preempt one mid-task (the bounded dependence of D5). Best-effort — a
    // denied request leaves it at default priority, which costs latency, never
    // correctness (the calling thread steals whatever a worker has not
    // claimed). Log the denial once per process so deployments know they run
    // degraded (issue #284); typical on Linux/Android without rtprio.
    if (!workers.back()->setPriority(true)) {
      static aBool priorityDenialLogged{false};
      if (!priorityDenialLogged.exchange(true)) {
        LogImpl().emit(E_WARNING, "render worker priority elevation denied by the OS; running "
                                  "at default priority (degraded real-time scheduling)");
      }
    }
  }
}

void YSE::INTERNAL::renderScheduler::shutdown() {
  if (!active) return;
  active = false;
  running.store(false, std::memory_order_seq_cst);
  // Unpark every worker so it observes !running and exits. The epoch bump is
  // ordered after the `running` store: a worker that read running == true
  // sampled the epoch before this bump, so its park either fails the compare
  // or is woken here. Control thread, not RT: wake unconditionally.
  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  wakeParked(wakeEpoch, -1);
  for (auto& w : workers)
    w->stop();
  workers.clear();
}

void YSE::INTERNAL::renderScheduler::beginBuild() {
  for (Int i = 0; i <= numWorkers; ++i) {
    lists[i].head = nullptr;
    lists[i].tail = nullptr;
    lists[i].cursor.store(nullptr, std::memory_order_relaxed);
  }
  leafCount = 0;
}

void YSE::INTERNAL::renderScheduler::addLeaf(renderTask& task) {
  // Round-robin by build order (D2); cost-based balancing is #861.
  leafList& l = lists[leafCount % (numWorkers + 1)];
  ++leafCount;
  task.leafNext = nullptr;
  task.dependencies = 0;
  task.pending.store(0, std::memory_order_relaxed);
  if (l.tail == nullptr)
    l.head = &task;
  else
    l.tail->leafNext = &task;
  l.tail = &task;
}

void YSE::INTERNAL::renderScheduler::setDependencies(renderTask& task, Int count) {
  task.leafNext = nullptr;
  task.dependencies = count;
  task.pending.store(count, std::memory_order_relaxed);
}

void YSE::INTERNAL::renderScheduler::endBuild() {
  dirty.store(false, std::memory_order_relaxed);
}

void YSE::INTERNAL::renderScheduler::runTask(renderTask& task) {
  task.execute(*this);
  // The root is the graph's single sink: nothing runs after it. Release pairs
  // with run()'s acquire, publishing every write of the block to the caller.
  if (&task == root) rootDone.store(true, std::memory_order_release);
}

void YSE::INTERNAL::renderScheduler::arrive(renderTask& task) {
  // acq_rel: each arrival publishes the finished dependency's writes, and the
  // thread that takes the count to zero acquires all of them before it runs
  // the task.
  if (task.pending.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
  // Last dependency: reset the counter for the next block before running.
  // Every arrival of this block has happened, so nothing else touches it until
  // the next block opens — and the next block opens only after run() has seen
  // every thread leave this one (inFlight), which orders this store before any
  // of next block's fetch_subs.
  task.pending.store(task.dependencies, std::memory_order_relaxed);
  runTask(task);
}

YSE::INTERNAL::renderTask* YSE::INTERNAL::renderScheduler::claim(Int list) {
  std::atomic<renderTask*>& cursor = lists[list].cursor;
  renderTask* t = cursor.load(std::memory_order_acquire);
  // The list is immutable during the block and the cursor only moves forward,
  // so there is no ABA: a failed CAS just means someone else claimed `t`.
  while (t != nullptr && !cursor.compare_exchange_weak(t, t->leafNext, std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) {}
  return t;
}

bool YSE::INTERNAL::renderScheduler::drainLeaves(Int self, bool passWake) {
  bool ran = false;
  const Int count = numWorkers + 1;
  // Own list first (affinity), then every other list in a fixed rotation.
  for (Int k = 0; k < count; ++k) {
    const Int list = (self + k) % count;
    while (renderTask* t = claim(list)) {
      // A worker woken by a partial wake hands it on as soon as it has work —
      // not after its whole drain, which would serialise the chain behind
      // every task it runs.
      if (passWake) {
        passWakeOn();
        passWake = false;
      }
      runTask(*t);
      ran = true;
    }
  }
  return ran;
}

void YSE::INTERNAL::renderScheduler::run(renderTask& rootTask) {
  // A graph without leaves has nothing that could ever reach the root (every
  // valid graph has at least one). Refuse it rather than spin forever.
  if (leafCount == 0) return;

  // Everything below is published to the workers by the seq_cst openGen
  // store: the cursors, the root, the cleared rootDone, and (through the
  // previous block's inFlight drain) every lazily reset dependency counter.
  for (Int i = 0; i <= numWorkers; ++i)
    lists[i].cursor.store(lists[i].head, std::memory_order_relaxed);
  root = &rootTask;
  rootDone.store(false, std::memory_order_relaxed);
  if (++lastOpened == 0) lastOpened = 1; // 0 means "closed"
  openGen.store(lastOpened, std::memory_order_seq_cst);

  wakeWorkers();

  drainLeaves(0, false);
  // Every list is empty and cursors only move forward, so no leaf is left to
  // run here: whatever remains is a task some worker is in the middle of, and
  // the root runs as its continuation. Pause-spin, never yield or sleep —
  // bounded by that one task's duration (D5).
  while (!rootDone.load(std::memory_order_acquire))
    cpuRelax();

  // Close the block, then wait for every worker to leave it (they finish
  // their steal scan and find the lists empty). After this, nothing reads the
  // leaf lists until the next run(), so the graph may be rebuilt.
  openGen.store(0, std::memory_order_seq_cst);
  while (inFlight.load(std::memory_order_seq_cst) != 0)
    cpuRelax();
  root = nullptr;
}

void YSE::INTERNAL::renderScheduler::wakeWorkers() {
  // RT path: one fence and one relaxed load, and a single non-blocking wake
  // call only when a worker is actually parked.
  const Int withLeaves = leafCount - 1 < numWorkers ? leafCount - 1 : numWorkers;
  if (withLeaves <= 0) return;

  // Store-load barrier pairing with the one in workerLoop(): the openGen
  // store must be visible before we read `parked`, and a parking worker's
  // `parked` increment before it re-checks openGen. At least one side sees
  // the other: we wake it, or it never parks.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const Int sleeping = parked.load(std::memory_order_relaxed);
  if (sleeping == 0) return;

  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  const Int wanted = withLeaves < sleeping ? withLeaves : sleeping;
  if (WAKE_COUNT_IS_EXACT)
    wakeParked(wakeEpoch, (int)wanted);
  else
    // Windows: all at once when the block needs every parked worker (no
    // worker's start waits on another's wake latency); otherwise one, and each
    // woken worker that finds work passes it on (#858's measured split).
    wakeParked(wakeEpoch, wanted >= sleeping ? -1 : 1);
}

void YSE::INTERNAL::renderScheduler::passWakeOn() {
  // Runs on a worker, never on the audio thread. futex wakes the right count
  // up front, so this is Windows only. The chain ends at the first woken
  // worker that finds nothing to run.
  if (WAKE_COUNT_IS_EXACT) return;
  if (parked.load(std::memory_order_relaxed) == 0) return;
  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  wakeParked(wakeEpoch, 1);
}

bool YSE::INTERNAL::renderScheduler::blockAvailable(std::uint32_t lastGen) const {
  const std::uint32_t g = openGen.load(std::memory_order_seq_cst);
  return g != 0 && g != lastGen;
}

bool YSE::INTERNAL::renderScheduler::spinForBlock(std::uint32_t lastGen) const {
  // The bounded post-block spin window of #858: long enough to catch the next
  // block of a back-to-back (offline) render, PAUSE for its first 10 us and
  // yield() for the rest so an oversubscribed pool does not starve the
  // rendering thread of a CPU.
  using clock = std::chrono::steady_clock;
  constexpr auto SPIN_WINDOW = std::chrono::microseconds(50);
  constexpr auto PAUSE_WINDOW = std::chrono::microseconds(10);
  const auto spinStart = clock::now();
  for (auto spun = clock::duration::zero();
       running.load(std::memory_order_relaxed) && spun < SPIN_WINDOW;
       spun = clock::now() - spinStart) {
    if (spun < PAUSE_WINDOW)
      cpuRelax();
    else
      std::this_thread::yield();
    if (blockAvailable(lastGen)) return true;
  }
  return false;
}

void YSE::INTERNAL::renderScheduler::workerLoop(Int self) {
  // Workers run DSP: MXCSR/FPCR is per-thread, so set FTZ/DAZ here too (issue
  // #81, denormalGuard.h).
  enableFlushToZero();

  std::uint32_t lastGen = 0; // the last block this worker took part in
  bool woken = false; // just returned from parkOn()

  while (running.load(std::memory_order_seq_cst)) {
    // Join the open block, if it is one we have not worked yet. Announce first
    // (inFlight), then read openGen: run() closes before it drains inFlight,
    // so either we see the block closed or run() waits for us to leave it.
    bool joined = false;
    inFlight.fetch_add(1, std::memory_order_seq_cst);
    const std::uint32_t g = openGen.load(std::memory_order_seq_cst);
    if (g != 0 && g != lastGen) {
      lastGen = g;
      joined = true;
      drainLeaves(self, woken);
    }
    inFlight.fetch_sub(1, std::memory_order_seq_cst);
    woken = false;

    // Spin only after taking part in a block, when the next one may be
    // moments away. A worker woken to nothing goes straight back to sleep.
    if (joined && spinForBlock(lastGen)) continue;

    // Park. Sample the epoch, announce, then re-check — see wakeWorkers() and
    // shutdown() for the pairing arguments. Any wake after the sample changes
    // the epoch, so parkOn() returns at once instead of sleeping through it.
    const std::uint32_t epoch = wakeEpoch.load(std::memory_order_seq_cst);
    parked.fetch_add(1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!running.load(std::memory_order_seq_cst) || blockAvailable(lastGen)) {
      parked.fetch_sub(1, std::memory_order_seq_cst);
      continue;
    }
    parkOn(wakeEpoch, epoch);
    parked.fetch_sub(1, std::memory_order_seq_cst);
    woken = true;
  }
}
