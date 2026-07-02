/*
  ==============================================================================

    threadPool.cpp
    Created: 1 Oct 2014 12:37:59pm
    Author:  yvan

  ==============================================================================
*/

#include "threadPool.h"
#include <assert.h>
#include "../system.hpp"
#include "denormalGuard.h"

YSE::INTERNAL::threadPoolJob::threadPoolJob() : shouldStop(false), inQueue(false), isDone(false) {}

YSE::INTERNAL::threadPoolJob::~threadPoolJob() {
  join();
}

void YSE::INTERNAL::threadPoolJob::join() {
  while (!isDone) {
    if (!inQueue) return;
    System().sleep(2);
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

YSE::INTERNAL::threadPool::threadPool(Int numThreads) : poolSize(numThreads), active(false) {
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
  }
}

void YSE::INTERNAL::threadPool::shutdown() {
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!active) return; // already shut down — nothing to join or drain
    active = false;
    while (!jobs.empty()) {
      jobs.front()->inQueue = false;
      jobs.front()->isDone = true;
      jobs.pop();
    }
  }
  cv.notify_all();
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
  {
    std::unique_lock<std::mutex> lock(mutex);
    jobs.push(job);
  }
  cv.notify_one();
}

YSE::INTERNAL::threadPoolJob * YSE::INTERNAL::threadPool::getJob() {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [this] { return !jobs.empty() || !active; });
  if (jobs.empty()) return nullptr;
  threadPoolJob * result = jobs.front();
  jobs.pop();
  return result;
}