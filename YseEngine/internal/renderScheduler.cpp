/*
  ==============================================================================

    renderScheduler.cpp
    Task-graph render scheduler (issue #859, epic #856).

  ==============================================================================
*/

#include "renderScheduler.h"
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include "cpuTopology.h"
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
    if (requested < 0) return YSE::INTERNAL::renderScheduler::autoWorkerCount();
    if (requested > YSE::INTERNAL::renderScheduler::MAX_WORKERS)
      return YSE::INTERNAL::renderScheduler::MAX_WORKERS;
    return requested;
  }

  std::int64_t nowNs() {
    return (std::int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // Sampled blocks (issue #861): nanoseconds of task time this thread has
  // accumulated at the current nesting level. A timed task saves it, runs,
  // and reads back how much of its own time went to continuations it ran
  // inline, so each task is charged only its own work.
  thread_local std::int64_t tlsTimedNs = 0;

  constexpr float TASK_COST_WEIGHT = 0.25f; // EMA weight of a new task sample
  constexpr float BLOCK_COST_FALL = 0.125f; // EMA weight when a block got cheaper
  constexpr float WAKE_COST_WEIGHT = 0.25f;
  // Gating hysteresis: serial below one wake, parallel again only above 1.5.
  constexpr float GATE_REOPEN_FACTOR = 1.5f;

  // How long a worker spins for the next block before it parks (#858). Also
  // what makes two blocks "back to back" for the serial gate (#861).
  constexpr auto WORKER_SPIN_WINDOW = std::chrono::microseconds(50);
  constexpr std::int64_t WORKER_SPIN_WINDOW_NS =
      std::chrono::duration_cast<std::chrono::nanoseconds>(WORKER_SPIN_WINDOW).count();
} // namespace

Int YSE::INTERNAL::renderScheduler::autoWorkerCount() {
  return autoWorkerCount(cpuTopology::machine());
}

Int YSE::INTERNAL::renderScheduler::autoWorkerCount(const cpuTopology& topology) {
  // Physical cores the process may run on (issue #862): SMT siblings share
  // one core's execution units, and cores outside the affinity mask (a
  // container's cpuset, a masked process) cannot take a worker at all. On a
  // hybrid part efficiency cores count too: measured on a 4 Zen 5 + 8 Zen 5c
  // part, the heavy scenes keep scaling onto them (Tests/TEST_PLAN.md, #862)
  // — limiting the default to the performance cores cost ~75% there. What
  // hybrid awareness changes is placement: performance cores are filled
  // first. No topology: the #861 fallback on logical CPUs.
  Int cores = static_cast<Int>(topology.cores.size());
  if (cores <= 0) cores = (Int)std::thread::hardware_concurrency();
  // The calling (audio) thread renders too, and the host keeps a core.
  Int workers = cores - 1;
  if (workers > MAX_AUTO_WORKERS) workers = MAX_AUTO_WORKERS;
  if (workers < 0) workers = 0;
  return workers;
}

// One render worker thread. Its list index is fixed for its lifetime.
class YSE::INTERNAL::renderScheduler::worker : public YSE::INTERNAL::thread {
public:
  worker(renderScheduler& owner, Int index, const cpuTopology::core* place)
    : place(place), owner(owner), index(index) {}
  void run() override {
    // Soft placement (issue #862), once, before the first block: an ideal
    // processor / performance-cluster hint, never a hard pin. Best-effort —
    // a declined hint leaves the OS to place the thread, as before.
    const bool accepted = place != nullptr && cpuTopology::machine().placeCurrentThread(*place);
    hint.store(accepted ? 1 : -1, std::memory_order_release);
    owner.workerLoop(index);
  }

  const cpuTopology::core* const place; // nullptr: no topology, no hint
  // 0 until the worker has tried its hint; then 1 accepted, -1 not applied.
  std::atomic<int> hint{0};

private:
  renderScheduler& owner;
  Int index;
};

YSE::INTERNAL::renderScheduler::renderScheduler(Int requested)
  : numWorkers(resolveWorkerCount(requested)),
    requestedWorkers(requested < 0 ? -1 : requested),
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
  requestedWorkers = requested < 0 ? -1 : requested;
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
  // Placement plan (issue #862): performance cores first, entry 0 left to the
  // calling thread. Empty when the topology is unknown — then no hints.
  const std::vector<const cpuTopology::core*> order = cpuTopology::machine().placementOrder();
  for (Int i = 1; i <= numWorkers; ++i) {
    const cpuTopology::core* place =
        order.empty() ? nullptr : order[static_cast<std::size_t>(i) % order.size()];
    workers.push_back(std::make_unique<worker>(*this, i, place));
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

const YSE::INTERNAL::cpuTopology::core*
YSE::INTERNAL::renderScheduler::workerCore(Int index) const {
  if (index < 1 || index > static_cast<Int>(workers.size())) return nullptr;
  return workers[static_cast<std::size_t>(index) - 1]->place;
}

Int YSE::INTERNAL::renderScheduler::placementHintsAccepted() const {
  Int count = 0;
  for (const auto& w : workers)
    if (w->hint.load(std::memory_order_acquire) == 1) ++count;
  return count;
}

Int YSE::INTERNAL::renderScheduler::placementHintsPending() const {
  Int count = 0;
  for (const auto& w : workers)
    if (w->hint.load(std::memory_order_acquire) == 0) ++count;
  return count;
}

std::string YSE::INTERNAL::renderScheduler::describePlacement() const {
  const cpuTopology& topo = cpuTopology::machine();
  std::string line = "render workers: " + std::to_string(numWorkers) +
                     (requestedWorkers < 0 ? " (auto)" : " (requested)");
  if (topo.cores.empty()) return line + "; CPU topology unknown, no placement hints";
  line += "; " + std::to_string(topo.cores.size()) + " physical cores";
  if (topo.hybrid)
    line += " (hybrid: " + std::to_string(topo.performanceCores()) + " performance, " +
            std::to_string(topo.cores.size() - static_cast<std::size_t>(topo.performanceCores())) +
            " efficiency)";
  if (workers.empty()) return line;
  line += "; cores:";
  for (const auto& w : workers)
    line += " " + (w->place != nullptr ? cpuTopology::describe(*w->place) : std::string("-"));
  return line;
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
    lists[i].assigned = 0.f;
  }
  leafCount = 0;
  buildUnmeasured = false;
}

void YSE::INTERNAL::renderScheduler::addLeaf(renderTask& task) {
  // Cost-driven deal (issue #861): the list with the least measured work so
  // far takes the leaf, lowest index on a tie. An unmeasured leaf weighs 1 ns,
  // so a graph with no measurements is dealt round-robin in call order (D2's
  // deal), and the build asks for one rebuild once the next sample is in.
  Int best = 0;
  for (Int i = 1; i <= numWorkers; ++i) {
    if (lists[i].assigned < lists[best].assigned) best = i;
  }
  float weight = task.cost();
  if (weight <= 0.f) {
    weight = 1.f;
    buildUnmeasured = true;
  }
  leafList& l = lists[best];
  l.assigned += weight;
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
  // A new graph is timed straight away, so the gate and the costs catch up
  // with the change; a deal made partly blind is redone once they have.
  forceSample.store(true, std::memory_order_relaxed);
  rebalanceAfterSample = buildUnmeasured;
}

void YSE::INTERNAL::renderScheduler::runTask(renderTask& task) {
  if (sampling.load(std::memory_order_relaxed))
    runTimed(task);
  else
    task.execute(*this);
  // The root is the graph's single sink: nothing runs after it. Release pairs
  // with run()'s acquire, publishing every write of the block to the caller.
  if (&task == root) rootDone.store(true, std::memory_order_release);
}

void YSE::INTERNAL::renderScheduler::runTimed(renderTask& task) {
  // One clock pair per task on a sampled block (issue #861). Continuations
  // this task runs inline through arrive() time themselves and add their
  // elapsed time to tlsTimedNs, which is subtracted here: each task's cost is
  // its own work, and the nesting level above sees the whole elapsed time.
  const std::int64_t outer = tlsTimedNs;
  tlsTimedNs = 0;
  const std::int64_t start = nowNs();
  task.execute(*this);
  const std::int64_t elapsed = nowNs() - start;
  const std::int64_t own = elapsed - tlsTimedNs;
  tlsTimedNs = outer + elapsed;

  // A task's cost is only ever written here (by the one thread running it
  // this block) or between blocks, so load-modify-store is enough. Clamp to
  // 1 ns: a coarse clock can read 0, and 0 means "never measured".
  float sample = (float)(own > 0 ? own : 0);
  if (sample < 1.f) sample = 1.f;
  const float prev = task.costNs.load(std::memory_order_relaxed);
  const float next = prev <= 0.f ? sample : prev + TASK_COST_WEIGHT * (sample - prev);
  task.costNs.store(next, std::memory_order_relaxed);
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

bool YSE::INTERNAL::renderScheduler::drainLeaves(Int self, bool passWake, bool sample) {
  bool ran = false;
  const Int count = numWorkers + 1;
  // On a sampled block, everything this thread runs from here — leaves and the
  // continuations they trigger — adds up in tlsTimedNs.
  if (sample) tlsTimedNs = 0;
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
  if (sample) lists[self].work.fetch_add(tlsTimedNs, std::memory_order_relaxed);
  return ran;
}

void YSE::INTERNAL::renderScheduler::run(renderTask& rootTask) {
  // A graph without leaves has nothing that could ever reach the root (every
  // valid graph has at least one). Refuse it rather than spin forever.
  if (leafCount == 0) return;

  // Time this block's tasks? (issue #861)
  const bool sample = forceSample.exchange(false, std::memory_order_relaxed) ||
                      blockCounter % COST_SAMPLE_PERIOD == 0;
  ++blockCounter;
  // One clock read at each end of every block: a block that starts within the
  // workers' post-block spin window of the last one is back to back.
  const std::int64_t blockStart = nowNs();
  const bool backToBack =
      lastBlockEndNs != 0 && blockStart - lastBlockEndNs < WORKER_SPIN_WINDOW_NS;
  const gate g = decideGate(backToBack);
  const bool serial = g == gate::serial;
  lastSerial.store(serial, std::memory_order_relaxed);

  // Everything below is published to the workers by the seq_cst openGen
  // store: the cursors, the root, the cleared rootDone, the sampling flag and
  // (through the previous block's inFlight drain) every lazily reset
  // dependency counter.
  for (Int i = 0; i <= numWorkers; ++i) {
    lists[i].cursor.store(lists[i].head, std::memory_order_relaxed);
    if (sample) lists[i].work.store(0, std::memory_order_relaxed);
  }
  root = &rootTask;
  rootDone.store(false, std::memory_order_relaxed);
  sampling.store(sample, std::memory_order_relaxed);

  if (serial) {
    runSerial(sample);
    return;
  }

  if (++lastOpened == 0) lastOpened = 1; // 0 means "closed"
  // Wake probe (issue #861): on a sampled block, the first worker that was
  // parked and joins this block stamps the time it got here.
  if (sample) firstJoinNs.store(0, std::memory_order_relaxed);
  probeGen.store(sample ? lastOpened : 0, std::memory_order_relaxed);
  wakeStartNs = blockStart;
  openGen.store(lastOpened, std::memory_order_seq_cst);

  const bool woke = g == gate::openAndWake && wakeWorkers();

  drainLeaves(0, false, sample);
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

  lastBlockEndNs = nowNs();
  if (sample) finishSample(woke, blockStart, lastBlockEndNs);
}

YSE::INTERNAL::renderScheduler::gate YSE::INTERNAL::renderScheduler::decideGate(bool backToBack) {
  // Serial gating (issue #861). No workers (or workers shut down): the calling
  // thread runs everything, which is the serial path. Before the first
  // measurement, and with gating off, blocks open and wake as before.
  if (numWorkers == 0 || !running.load(std::memory_order_relaxed)) return gate::serial;
  const float block = blockCostNs.load(std::memory_order_relaxed);
  if (!gating.load(std::memory_order_relaxed) || block < 0.f) {
    lightMode = false;
    return gate::openAndWake;
  }
  // A block is "light" when it is estimated to cost less than one wake:
  // signalling a parked worker would cost more than it could save.
  // Hysteresis keeps a scene near the threshold from flapping: once light, it
  // turns heavy again only above 1.5 wakes.
  const float wake = wakeCostNs.load(std::memory_order_relaxed);
  lightMode = lightMode ? block <= wake * GATE_REOPEN_FACTOR : block < wake;
  if (!lightMode) return gate::openAndWake;
  // A light block still opens when the workers are cheap to reach. Blocks
  // arriving back to back (offline rendering) keep the workers in their
  // post-block spin, where joining costs a cache miss rather than a wake:
  // measured on the 100-sound scene, that beats rendering alone even for a
  // couple of microseconds of work, so such blocks open and wake — only the
  // first wake is a real one. Otherwise a worker that has not parked yet is
  // still welcome, but nobody is woken for a light block. Once every worker
  // has parked and blocks are far apart — real-time rendering, a block every
  // few milliseconds — the calling thread renders alone.
  if (backToBack) return gate::openAndWake;
  if (parked.load(std::memory_order_relaxed) < numWorkers) return gate::openNoWake;
  return gate::serial;
}

void YSE::INTERNAL::renderScheduler::runSerial(bool sample) {
  // The block never opens (openGen stays 0), so no worker — parked, spinning
  // or about to park — can join it: the calling thread claims every leaf, and
  // every continuation runs inline behind the leaf that satisfies it. No wake,
  // no inFlight drain. Same leaf lists, same claim path, same arrive protocol
  // as a parallel block, so the result is the same.
  drainLeaves(0, false, sample);
  // Every leaf ran on this thread, and with it every continuation: the root
  // is done. (Not spun on — nothing else could finish it.)
  root = nullptr;
  lastBlockEndNs = nowNs();
  if (sample) finishSample(false, 0, 0);
}

void YSE::INTERNAL::renderScheduler::finishSample(bool woke, std::int64_t blockStart,
                                                  std::int64_t blockEnd) {
  // Calling thread, after the block has closed and every worker has left it
  // (their work counters and the probe stamp happen-before the inFlight
  // drain, or there were no workers).
  std::int64_t total = 0;
  for (Int i = 0; i <= numWorkers; ++i)
    total += lists[i].work.load(std::memory_order_relaxed);
  const float work = (float)total;
  // The block estimate rises at once and falls slowly: a scene that just got
  // heavy must leave serial rendering at the next decision, a lighter one can
  // wait a few samples.
  const float prev = blockCostNs.load(std::memory_order_relaxed);
  const float next = (prev < 0.f || work > prev) ? work : prev + BLOCK_COST_FALL * (work - prev);
  blockCostNs.store(next, std::memory_order_relaxed);

  if (woke) {
    // The wake cost: from the wake call to the first parked worker joining.
    // If none joined before the block closed, the wake took longer than the
    // whole block — the block's duration is only a lower bound, so it may
    // raise the estimate but never lower it (averaging it in would drag the
    // estimate down to the duration of short blocks and open light ones).
    const std::int64_t joined = firstJoinNs.load(std::memory_order_relaxed);
    const float w = wakeCostNs.load(std::memory_order_relaxed);
    if (joined != 0) {
      std::int64_t latency = joined - wakeStartNs;
      if (latency < 0) latency = 0;
      wakeCostNs.store(w + WAKE_COST_WEIGHT * ((float)latency - w), std::memory_order_relaxed);
    } else {
      const auto atLeast = (float)(blockEnd - blockStart);
      if (atLeast > w)
        wakeCostNs.store(w + WAKE_COST_WEIGHT * (atLeast - w), std::memory_order_relaxed);
    }
  }
  probeGen.store(0, std::memory_order_relaxed);

  if (rebalanceAfterSample) {
    // The last build dealt some leaves blind; every leaf now has a cost.
    rebalanceAfterSample = false;
    markDirty();
  }
}

bool YSE::INTERNAL::renderScheduler::wakeWorkers() {
  // RT path: one fence and one relaxed load, and a single non-blocking wake
  // call only when a worker is actually parked. Returns whether it woke any.
  const Int withLeaves = leafCount - 1 < numWorkers ? leafCount - 1 : numWorkers;
  if (withLeaves <= 0) return false;

  // Store-load barrier pairing with the one in workerLoop(): the openGen
  // store must be visible before we read `parked`, and a parking worker's
  // `parked` increment before it re-checks openGen. At least one side sees
  // the other: we wake it, or it never parks.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const Int sleeping = parked.load(std::memory_order_relaxed);
  if (sleeping == 0) return false;

  wakeEpoch.fetch_add(1, std::memory_order_seq_cst);
  const Int wanted = withLeaves < sleeping ? withLeaves : sleeping;
  if (WAKE_COUNT_IS_EXACT)
    wakeParked(wakeEpoch, (int)wanted);
  else
    // Windows: all at once when the block needs every parked worker (no
    // worker's start waits on another's wake latency); otherwise one, and each
    // woken worker that finds work passes it on (#858's measured split).
    wakeParked(wakeEpoch, wanted >= sleeping ? -1 : 1);
  return true;
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
  constexpr auto PAUSE_WINDOW = std::chrono::microseconds(10);
  const auto spinStart = clock::now();
  for (auto spun = clock::duration::zero();
       running.load(std::memory_order_relaxed) && spun < WORKER_SPIN_WINDOW;
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
      // The wake probe (issue #861): a worker straight out of its park joining
      // the probed block stamps its arrival; the first stamp wins. Only then
      // is the clock read, so a block without a probe costs nothing here.
      if (woken && probeGen.load(std::memory_order_relaxed) == g) {
        std::int64_t unset = 0;
        firstJoinNs.compare_exchange_strong(unset, nowNs(), std::memory_order_relaxed);
      }
      drainLeaves(self, woken, sampling.load(std::memory_order_relaxed));
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
