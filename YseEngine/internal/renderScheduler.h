/*
  ==============================================================================

    renderScheduler.h
    Task-graph render scheduler (issue #859, epic #856).

  ==============================================================================
*/

#ifndef RENDERSCHEDULER_H_INCLUDED
#define RENDERSCHEDULER_H_INCLUDED

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    class renderScheduler;

    /** One node of the render task graph (issue #859).

        A task is either a *leaf* — placed on a worker's leaf list when the
        graph is built, claimed from that list (or stolen from another's) once
        per block — or a *continuation*: it has dependencies and is never
        queued. Whoever satisfies its last dependency (renderScheduler::arrive)
        runs it inline, on the same thread, right away.

        A task body must arrive() at every successor it has, every block, on
        every path — the dependency counters are reset lazily by the thread that
        takes each one to zero, so a skipped arrival would stall the block.

        The link and the counters are owned by the scheduler; they are touched
        only by the audio thread while it builds the graph (workers parked) and
        by the scheduler's own claim/arrive protocol during a block.
    */
    class renderTask {
    public:
      renderTask() = default;
      virtual ~renderTask() = default;
      renderTask(const renderTask&) = delete;
      renderTask& operator=(const renderTask&) = delete;
      renderTask(renderTask&&) = delete;
      renderTask& operator=(renderTask&&) = delete;

      /** The task body. Runs exactly once per block, on the thread that
          claimed it (leaf) or that satisfied its last dependency
          (continuation). Must not allocate, lock or block. */
      virtual void execute(renderScheduler& scheduler) = 0;

      /** Measured cost of this task (issue #861): an exponential moving
          average of execute()'s own render time in nanoseconds, excluding any
          continuation it ran inline through arrive(). 0 until the task has
          been timed once. Sampled on a fraction of the blocks (see
          renderScheduler::COST_SAMPLE_PERIOD). Written by whichever thread ran
          the task in a sampled block; read between blocks (by the graph
          builder and the voice-slice policy on the audio thread) or from any
          thread as a diagnostic — hence relaxed atomics. */
      float cost() const {
        return costNs.load(std::memory_order_relaxed);
      }

      /** Overwrite the cost estimate. Between blocks only (audio thread): an
          owner whose work changed in a known way — a voice slice gaining or
          losing a sound — keeps the estimate in step before the next sample.
          0 forgets the measurement. */
      void setCost(float ns) {
        costNs.store(ns < 0.f ? 0.f : ns, std::memory_order_relaxed);
      }

    private:
      friend class renderScheduler;
      renderTask* leafNext = nullptr; // next leaf on the same worker's list
      std::atomic<Int> pending{0}; // dependencies still outstanding this block
      Int dependencies = 0; // pending's value at the start of every block
      std::atomic<float> costNs{0.f}; // see cost()
    };

    /** The render scheduler (issue #859, design decisions D1-D6 on the issue).

        Owns the render workers and runs one block of a task graph per run()
        call. The graph is built on the audio thread, in place, whenever a
        structural change has marked it dirty (D1): the channel and return lists
        it is built from are audio-thread-owned, and every worker is out of the
        block before run() returns, so a rebuild never overlaps a reader.

        Each worker — the calling (audio) thread is worker 0 — owns an immutable
        intrusive list of leaf tasks and an atomic cursor into it. Claiming a
        leaf, own or stolen, is one CAS on that list's cursor. Non-leaf tasks
        are never enqueued: the thread whose arrive() takes a task's dependency
        count to zero runs it inline (D2). There is no job queue, no push path
        and no dynamic allocation on the block path.

        The calling thread renders too: it drains its own list, then steals from
        every other list, and only when every list is empty and the root has
        not run yet does it pause-spin, bounded by the duration of whatever task
        a worker is in the middle of (D5). With zero workers it runs the whole
        graph itself, through the same code path.

        Workers park in the kernel between blocks (the #858 primitive) and are
        woken with at most one wake call per block.

        Self-tuning (issue #861). Every COST_SAMPLE_PERIOD-th block (and the
        block after a rebuild or a requestSample()) each task is timed — one
        steady_clock pair per task — into its cost() average, and the block's
        total work into blockCost(). Leaves are dealt to the least-loaded list
        by measured cost. When the whole block is estimated to cost less than
        waking a worker (wakeCost(), itself measured from real wakes), nobody
        is woken for it: it goes to workers still spinning from the previous
        block (always the case when blocks arrive back to back, as in offline
        rendering), and when every worker has parked the calling thread
        renders it alone — serial gating. Gating changes who runs a task,
        never what it computes, so the output is identical either way.
    */
    class renderScheduler {
    public:
      // Upper bound on the auto-sized (numWorkers == -1) worker count (issue
      // #861). The auto size is the physical core count minus one — the
      // calling thread renders too, and the host application keeps a core —
      // clamped to this. Replaces the fan-out pool's MAX_AUTO_RENDER_THREADS
      // = 2 stopgap (#650): with serial gating a small scene no longer pays
      // for idle workers, so the cap only bounds how far a heavy one spreads.
      static constexpr Int MAX_AUTO_WORKERS = 8;

      // Upper bound on an explicit worker count; larger requests are clamped.
      static constexpr Int MAX_WORKERS = 64;

      // One block in this many has its tasks timed (plus forced samples).
      static constexpr std::uint32_t COST_SAMPLE_PERIOD = 16;

      // Wake-cost estimate before the first real wake has been measured.
      static constexpr float INITIAL_WAKE_COST_NS = 20000.f;

      // The auto-sized worker count on this machine: physical cores - 1,
      // clamped to [0, MAX_AUTO_WORKERS]. A single-core machine renders
      // serially.
      static Int autoWorkerCount();

      // numWorkers: -1 auto-sizes (autoWorkerCount()); 0 means no worker
      // threads — the calling thread runs every task. Spawns the workers.
      explicit renderScheduler(Int numWorkers = -1);
      ~renderScheduler();
      renderScheduler(const renderScheduler&) = delete;
      renderScheduler& operator=(const renderScheduler&) = delete;
      renderScheduler(renderScheduler&&) = delete;
      renderScheduler& operator=(renderScheduler&&) = delete;

      // Resolved worker count (not counting the calling thread).
      Int workerCount() const {
        return numWorkers;
      }

      // The count last passed to the constructor or setWorkerCount(), before
      // resolution: -1 means auto.
      Int requestedWorkerCount() const {
        return requestedWorkers;
      }

      /////////////////////////////////////////////////////
      // Cost tracking and serial gating (issue #861)
      /////////////////////////////////////////////////////

      // Serial gating on (default) or off. Off, every block with workers opens
      // to them regardless of its cost — the test hook that keeps parallel
      // paths exercised by scenes too small to be worth it. Any thread; takes
      // effect at the next block.
      void setSerialGating(bool on) {
        gating.store(on, std::memory_order_relaxed);
      }
      bool serialGating() const {
        return gating.load(std::memory_order_relaxed);
      }

      // Whether the last block ran on the calling thread alone (no workers,
      // or gated serial). Diagnostic.
      bool lastBlockSerial() const {
        return lastSerial.load(std::memory_order_relaxed);
      }

      // Estimated total work of one block, in nanoseconds (sum of every
      // task's own time); -1 before the first measurement. Diagnostic.
      float blockCost() const {
        return blockCostNs.load(std::memory_order_relaxed);
      }

      // Measured latency from a wake call to the first woken worker taking
      // part in the block, in nanoseconds. INITIAL_WAKE_COST_NS until a wake
      // has been measured. Diagnostic, and the voice-slice size target.
      float wakeCost() const {
        return wakeCostNs.load(std::memory_order_relaxed);
      }

      // Overwrite the wake-cost estimate (later wakes keep averaging into it).
      // Test hook: makes "light" and "heavy" independent of how fast this
      // machine — or a sanitizer build — happens to wake a thread.
      void setWakeCost(float ns) {
        wakeCostNs.store(ns, std::memory_order_relaxed);
      }

      // Time the tasks of the next block, whatever the sample period says. For
      // owners that just changed a task's work (a sound joined or left a
      // voice slice). Any thread; a single relaxed store, like markDirty().
      void requestSample() {
        forceSample.store(true, std::memory_order_relaxed);
      }

      // Re-size (issue #857 hook, policy in #861): stop the workers, record
      // the new count, and re-spawn them if the scheduler was running. -1
      // re-applies the auto-sizing rule. Marks the graph dirty, since leaves
      // are dealt out per worker. Control thread only, and only while nothing
      // renders — it joins and spawns threads.
      void setWorkerCount(Int numWorkers);

      // (Re)spawn the workers. Idempotent. Control thread.
      void startup();
      // Wake and join every worker. Idempotent. Control thread. run() stays
      // usable afterwards: the calling thread then steals every leaf itself.
      void shutdown();

      // Workers currently parked, waiting for a block. Test hook.
      Int parkedWorkers() const {
        return parked.load(std::memory_order_acquire);
      }

      // Cost the last build dealt to leaf list @p list (0 = the calling
      // thread's), unmeasured leaves counting 1 ns each; 0 when out of range.
      // Test hook, between blocks.
      float assignedCost(Int list) const {
        return (list < 0 || list > numWorkers) ? 0.f : lists[list].assigned;
      }

      /////////////////////////////////////////////////////
      // Graph building — audio thread, between blocks
      /////////////////////////////////////////////////////

      // Flag a structural change (a channel connected or disconnected, a
      // return linked, unlinked or re-generationed, a new master). Any thread;
      // a single relaxed store.
      void markDirty() {
        dirty.store(true, std::memory_order_relaxed);
      }
      bool isDirty() const {
        return dirty.load(std::memory_order_relaxed);
      }

      // Start a new graph: forget every leaf list. Allocation-free.
      void beginBuild();
      // Put a dependency-free task on the leaf list with the least measured
      // cost so far in this build (issue #861); ties go to the lowest list,
      // and an unmeasured task counts as a minimal one, so a graph with no
      // measurements yet is dealt round-robin in call order.
      void addLeaf(renderTask& task);
      // Declare a continuation with `count` dependencies (count > 0). Each
      // dependency must arrive() at it exactly once per block.
      void setDependencies(renderTask& task, Int count);
      // Finish the graph and clear the dirty flag. The next block is timed; if
      // any leaf was still unmeasured, the graph is rebuilt once after that
      // sample, so the deal reflects real costs.
      void endBuild();

      /////////////////////////////////////////////////////
      // Block execution
      /////////////////////////////////////////////////////

      // Run one block of the current graph, from the calling thread, and
      // return once `root` has run and every worker has left the block. Audio
      // thread (the one thread that renders). No allocation, no lock; the only
      // system call is the one wake, and only when a worker is parked.
      void run(renderTask& root);

      // A dependency of `task` is complete. From a task body only. If this was
      // the last one, `task` runs inline on the calling thread before arrive()
      // returns.
      void arrive(renderTask& task);

    private:
      // A worker's leaf list and claim cursor, on its own cache line so
      // claims on different lists do not false-share.
      struct alignas(64) leafList {
        renderTask* head = nullptr;
        renderTask* tail = nullptr;
        std::atomic<renderTask*> cursor{nullptr};
        float assigned = 0.f; // build only: cost dealt to this list so far
        // Sampled blocks: nanoseconds of task work run by this list's thread
        // (one writer — that thread; read by the calling thread after the
        // block has closed).
        std::atomic<std::int64_t> work{0};
      };

      class worker;

      void runTask(renderTask& task);
      void runTimed(renderTask& task);
      // What run() does with the next block (issue #861): render it alone,
      // open it only to workers that have not parked yet, or open it and
      // wake parked workers.
      enum class gate { serial, openNoWake, openAndWake };
      gate decideGate(bool backToBack);
      void runSerial(bool sample);
      void finishSample(bool woke, std::int64_t blockStart, std::int64_t blockEnd);
      renderTask* claim(Int list);
      // Own list, then steal from the others; true if any leaf ran. A worker
      // woken by a partial wake passes it on at its first claim (passWake).
      bool drainLeaves(Int self, bool passWake, bool sample);
      bool blockAvailable(std::uint32_t lastGen) const;
      bool spinForBlock(std::uint32_t lastGen) const; // bounded post-block spin
      void workerLoop(Int self);
      bool wakeWorkers();
      void passWakeOn();

      Int numWorkers = 0;
      Int requestedWorkers = -1;
      bool active = false; // control thread only
      std::atomic<bool> running{false}; // read by the workers

      // numWorkers + 1 lists; list 0 is the calling thread's. Re-allocated
      // only by setWorkerCount().
      std::unique_ptr<leafList[]> lists;
      Int leafCount = 0; // leaves dealt out by the current build
      std::vector<std::unique_ptr<worker>> workers;

      std::atomic<bool> dirty{true};

      // Block protocol. `openGen` is 0 between blocks and a fresh non-zero
      // generation while one is open; a worker joins a block by incrementing
      // `inFlight` and only then reading `openGen` (both seq_cst), and run()
      // closes it by storing 0 and then waiting for `inFlight` to drain — so
      // no worker is still inside a block (reading leaf lists or cursors) once
      // run() returns, and the next rebuild cannot race one.
      std::atomic<std::uint32_t> openGen{0};
      std::uint32_t lastOpened = 0; // calling thread only
      std::atomic<Int> inFlight{0};
      renderTask* root = nullptr; // written before the block opens
      std::atomic<bool> rootDone{false};

      // Park/wake (the #858 primitive): parked workers wait on `wakeEpoch`;
      // every wake bumps it, so a worker that sampled it before announcing
      // itself in `parked` can never sleep through a wake that raced its park.
      std::atomic<std::uint32_t> wakeEpoch{0};
      std::atomic<Int> parked{0};

      // Cost tracking and gating (issue #861). `sampling` is written by the
      // calling thread before the block opens and read by the workers inside
      // it. The estimates are written by the calling thread between blocks.
      std::atomic<bool> gating{true};
      std::atomic<bool> sampling{false};
      std::atomic<bool> lastSerial{false};
      std::atomic<float> blockCostNs{-1.f};
      std::atomic<float> wakeCostNs{INITIAL_WAKE_COST_NS};
      bool lightMode = false; // gating hysteresis state, calling thread only
      std::atomic<bool> forceSample{true}; // see requestSample()
      bool rebalanceAfterSample = false; // calling thread only
      bool buildUnmeasured = false; // build only: a leaf had no cost yet
      std::uint32_t blockCounter = 0; // calling thread only
      std::int64_t lastBlockEndNs = 0; // calling thread only; 0 = none yet
      // Wake probe: on a sampled block that issued a wake, the calling thread
      // records when it woke the workers and publishes the block's generation
      // in `probeGen`; the first woken worker to join that block stamps
      // `firstJoinNs`.
      std::int64_t wakeStartNs = 0; // calling thread only
      std::atomic<std::uint32_t> probeGen{0};
      std::atomic<std::int64_t> firstJoinNs{0};
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // RENDERSCHEDULER_H_INCLUDED
