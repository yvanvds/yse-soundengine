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

YSE::INTERNAL::threadPoolThread::threadPoolThread(threadPool * pool, Int sleepTimeMS) : pool(pool), sleepTime(sleepTimeMS) {}

void YSE::INTERNAL::threadPoolThread::run() {
  while (!threadShouldExit()) {
    threadPoolJob * job = pool->getJob();
    if (job != nullptr) {
      job->activate();
    }
    else {
      std::this_thread::sleep_for(sleepTime);
    }
  }
}

YSE::INTERNAL::threadPool::threadPool(Int sleepTime, Int numThreads) : active(true){
  if (numThreads == -1) {
    numThreads = std::thread::hardware_concurrency();
  }

  // this might happen if hardware_concurrency() is not well defined or not computable
  // in which case we need at least one thread to continue
  if (numThreads == 0) {
    numThreads = 1;
  }

  for (Int i = 0; i < numThreads; i++) {
    threads.emplace_front(this, sleepTime);
    threads.front().start();
  }
}

YSE::INTERNAL::threadPool::~threadPool() {

}

void YSE::INTERNAL::threadPool::shutdown() {
  active = false;
  mutex.lock();
  for (auto i = threads.begin(); i != threads.end(); ++i) {
    i->stop();
  }
  while (!jobs.empty()) {
    jobs.front()->inQueue = false;
    jobs.front()->isDone = true;
    jobs.pop();
  }

  mutex.unlock();
}

void YSE::INTERNAL::threadPool::addJob(threadPoolJob * job) {
  // if no threads, shutdown is called
  if (!active) return;

  job->start();
  mutex.lock();
  jobs.push(job);
  mutex.unlock();
}

YSE::INTERNAL::threadPoolJob * YSE::INTERNAL::threadPool::getJob() {
  if (!active) return nullptr;

  mutex.lock();
  threadPoolJob * result;
  if (jobs.empty()) {
    result = nullptr;
  }
  else {
    result = jobs.front();
    jobs.pop();
  }

  mutex.unlock();
  return result;
}