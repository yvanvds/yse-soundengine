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

    private:
      friend class renderScheduler;
      renderTask* leafNext = nullptr; // next leaf on the same worker's list
      std::atomic<Int> pending{0}; // dependencies still outstanding this block
      Int dependencies = 0; // pending's value at the start of every block
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
    */
    class renderScheduler {
    public:
      // Upper bound on the auto-sized (numWorkers == -1) worker count. Carried
      // over from the channel fan-out pool (issue #650), where more workers
      // measured slower; the thread-count policy proper is #861.
      static constexpr Int MAX_AUTO_WORKERS = 2;

      // numWorkers: -1 auto-sizes (hardware_concurrency, capped at
      // MAX_AUTO_WORKERS); 0 means no worker threads — the calling thread runs
      // every task. Spawns the workers.
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
      // Put a dependency-free task on the next worker's leaf list (round-robin
      // over the calling thread and the workers, in call order).
      void addLeaf(renderTask& task);
      // Declare a continuation with `count` dependencies (count > 0). Each
      // dependency must arrive() at it exactly once per block.
      void setDependencies(renderTask& task, Int count);
      // Finish the graph and clear the dirty flag.
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
      };

      class worker;

      void runTask(renderTask& task);
      renderTask* claim(Int list);
      // Own list, then steal from the others; true if any leaf ran. A worker
      // woken by a partial wake passes it on at its first claim (passWake).
      bool drainLeaves(Int self, bool passWake);
      bool blockAvailable(std::uint32_t lastGen) const;
      bool spinForBlock(std::uint32_t lastGen) const; // bounded post-block spin
      void workerLoop(Int self);
      void wakeWorkers();
      void passWakeOn();

      Int numWorkers = 0;
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
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // RENDERSCHEDULER_H_INCLUDED
