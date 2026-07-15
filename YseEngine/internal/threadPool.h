/*
  ==============================================================================

    threadPool.h
    Created: 1 Oct 2014 12:37:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef THREADPOOL_H_INCLUDED
#define THREADPOOL_H_INCLUDED

#include "../headers/types.hpp"
#include "../utils/mpmcQueue.hpp"
#include <atomic>
#include <forward_list>
#include "thread.h"

namespace YSE {
  namespace INTERNAL {
    class threadPool;

    // How a pool behaves on the audio-callback path (issue #188).
    enum class poolClass {
      // Render fan-out: workers run at raised priority and, if the job ring is
      // ever full, the producer runs the job inline rather than dropping it.
      render,
      // Background work (manager setup/delete, file loading, stream refill):
      // default-priority workers; a fire-and-forget queue that must never run
      // inline on the caller (the caller may be the audio thread and the work
      // touches disk).
      background,
    };

    class threadPoolJob {
    public:
      threadPoolJob();
      virtual ~threadPoolJob();

      virtual void run() = 0;

      void join();
      void stop() {
        shouldStop = true;
      }
      void start() {
        shouldStop = false;
        isDone = false;
        inQueue = true;
      }
      void activate(); // this is called by the threadPoolThread
      bool isQueued() {
        return inQueue;
      }

    private:
      aBool shouldStop;
      aBool inQueue;
      aBool isDone;
      // The render pool this job was last queued on, set by threadPool::addJob
      // so join() can help-run sibling jobs from the same ring instead of
      // spinning while it waits (issue #284). Stays nullptr for background
      // pools: help-running there would put disk-touching work on the joining
      // thread. Atomic only to keep the cross-thread publish TSan-clean; the
      // pool (a process-lifetime singleton in engine use) always outlives any
      // join() that observes inQueue == true.
      std::atomic<threadPool*> pool;
      friend class threadPool;
    };

    class threadPoolThread : public thread {
    public:
      explicit threadPoolThread(threadPool* pool);
      virtual void run();

    private:
      threadPool* pool;
    };

    class threadPool {
    public:
      // numThreads: -1 means hardware_concurrency. cls selects the RT behaviour
      // (see poolClass).
      explicit threadPool(Int numThreads = -1, poolClass cls = poolClass::render);
      ~threadPool();

      // Wait-free on the producer side: pushes the job into the lock-free ring
      // and lets a worker pick it up. Never locks, allocates, or blocks — safe
      // to call from the audio callback. For a render pool, a full ring falls
      // back to running the job inline on the caller so no DSP work is dropped.
      void addJob(threadPoolJob* job);

      // only used by threadPoolThread, returns nullptr when the pool shuts down
      threadPoolJob* getJob();

      // Pop one queued job and run it on the calling thread; returns whether a
      // job was run. Used by threadPoolJob::join() to help-run sibling jobs
      // while it waits (issue #284): instead of burning its budget spinning on
      // a possibly-preempted worker, the audio callback drains runnable work
      // from the same render ring itself. Never locks, allocates, or blocks —
      // safe on the audio callback, same as the inline fallback in addJob().
      bool tryRunOne();

      // (Re)spawn the worker threads and mark the pool active. Called by the
      // constructor and, after a shutdown(), by global::init() to revive the
      // pool for a fresh engine session (issue #140). Idempotent: a no-op while
      // the pool is already active.
      void startup();

      // shutdown this pool: mark inactive, drain the ring, join every worker,
      // and drop the (now-joined) thread objects so a later startup() re-spawns
      // cleanly. Call before deconstructing; safe on an already-inactive pool.
      void shutdown();

    private:
      // Ring capacity. Render must hold one job per channel dispatched in a
      // render pass; background holds the handful of manager/setup/refill jobs.
      // Both are far above any realistic live count, and a full render ring
      // degrades gracefully to inline execution rather than dropping work.
      static constexpr std::size_t RENDER_CAPACITY = 4096;
      static constexpr std::size_t BACKGROUND_CAPACITY = 1024;

      mpmcQueue<threadPoolJob*> jobs;
      std::forward_list<threadPoolThread> threads;
      Int poolSize; // resolved worker count, reused when startup() re-spawns
      poolClass classOf; // render vs background behaviour
      aBool active;
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // THREADPOOL_H_INCLUDED
