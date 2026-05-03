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
#include <forward_list>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "thread.h"

namespace YSE {
  namespace INTERNAL {
    class threadPool;

    class threadPoolJob {
    public:
      threadPoolJob();
      ~threadPoolJob();

      virtual void run() = 0;

      void join();
      void stop() { shouldStop = true; }
      void start() { shouldStop = false;  isDone = false; inQueue = true; }
      void activate(); // this is called by the threadPoolThread
      bool isQueued() { return inQueue; }

    private:
      aBool shouldStop;
      aBool inQueue;
      aBool isDone;
      friend class threadPool;
    };

    class threadPoolThread : public thread {
    public:
      threadPoolThread(threadPool * pool);
      virtual void run();

    private:
      threadPool * pool;
    };

    class threadPool {
    public:
      // numThreads: -1 means hardware_concurrency
      threadPool(Int numThreads = -1);
      ~threadPool();

      void addJob(threadPoolJob * job);

      // only used by threadPoolThread, returns nullptr if there's no job to execute
      threadPoolJob * getJob();

      // shutdown this pool. Call this before deconstructing
      void shutdown();

    private:
      std::queue<threadPoolJob*> jobs;
      std::forward_list<threadPoolThread> threads;
      aBool active;
      std::mutex mutex;
      std::condition_variable cv;
    };

  }
}





#endif  // THREADPOOL_H_INCLUDED
