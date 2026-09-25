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
#include <forward_list>
#include "thread.h"

namespace YSE {
  namespace INTERNAL {
    class threadPool;

    // A fire-and-forget background job (manager setup/delete, file loading,
    // stream refill, patcher I/O). Rendering does not go through this pool:
    // it runs on the task-graph renderScheduler (issue #859).
    class threadPoolJob {
    public:
      threadPoolJob();
      virtual ~threadPoolJob();

      virtual void run() = 0;

      // Wait until the job is no longer queued or running. Spins, then
      // yields; never called from the audio callback.
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
      friend class threadPool;
    };

    class threadPoolThread : public thread {
    public:
      explicit threadPoolThread(threadPool* pool);
      void run() override;

    private:
      threadPool* pool;
    };

    // Background worker pool: default-priority workers draining a lock-free
    // job ring. A fire-and-forget queue that never runs a job inline on the
    // caller — the caller may be the audio thread and the work touches disk.
    class threadPool {
    public:
      // numThreads is clamped to at least one: nothing else ever runs a
      // background job, so a pool without workers would silently drop work.
      explicit threadPool(Int numThreads = 1);
      ~threadPool();

      // Resolved worker count.
      Int workerCount() const {
        return poolSize;
      }

      // Wait-free on the producer side: pushes the job into the lock-free ring
      // and lets a worker pick it up. Never locks, allocates, or blocks — safe
      // to call from the audio callback. A full ring (unreachable at the chosen
      // capacity) drops the job with its queued flag cleared; the manager and
      // refill schedulers re-enqueue on their next tick.
      void addJob(threadPoolJob* job);

      // only used by threadPoolThread, returns nullptr when the pool shuts down
      threadPoolJob* getJob();

      // (Re)spawn the worker threads and mark the pool active. Called by the
      // constructor and, after a shutdown(), by global::init() to revive the
      // pool for a fresh engine session (issue #140). Idempotent.
      void startup();

      // shutdown this pool: mark inactive, drain the ring, join every worker,
      // and drop the (now-joined) thread objects so a later startup() re-spawns
      // cleanly. Call before deconstructing; safe on an already-inactive pool.
      void shutdown();

    private:
      // Holds the handful of manager/setup/refill jobs in flight; far above
      // any realistic live count.
      static constexpr std::size_t CAPACITY = 1024;

      mpmcQueue<threadPoolJob*> jobs;
      std::forward_list<threadPoolThread> threads;
      Int poolSize; // resolved worker count, reused when startup() re-spawns
      aBool active;
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // THREADPOOL_H_INCLUDED
