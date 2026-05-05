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
  inQueue = false;
  isDone = true;
}

YSE::INTERNAL::threadPoolThread::threadPoolThread(threadPool * pool) : pool(pool) {}

void YSE::INTERNAL::threadPoolThread::run() {
  while (!threadShouldExit()) {
    threadPoolJob * job = pool->getJob();
    if (job == nullptr) return;
    job->activate();
  }
}

YSE::INTERNAL::threadPool::threadPool(Int numThreads) : active(true){
  if (numThreads == -1) {
    numThreads = std::thread::hardware_concurrency();
  }

  // this might happen if hardware_concurrency() is not well defined or not computable
  // in which case we need at least one thread to continue
  if (numThreads == 0) {
    numThreads = 1;
  }

  for (Int i = 0; i < numThreads; i++) {
    threads.emplace_front(this);
    threads.front().start();
  }
}

YSE::INTERNAL::threadPool::~threadPool() {
  shutdown();
}

void YSE::INTERNAL::threadPool::shutdown() {
  {
    std::unique_lock<std::mutex> lock(mutex);
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