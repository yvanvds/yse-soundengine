/*
  ==============================================================================

    global.cpp
    Created: 27 Jan 2014 10:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"


YSE::INTERNAL::global & YSE::INTERNAL::Global() {
    static global s;
    return s;
}

void YSE::INTERNAL::global::addSlowJob(threadPoolJob * job) {
  slowThreads.addJob(job);
}

void YSE::INTERNAL::global::addFastJob(threadPoolJob * job) {
  fastThreads.addJob(job);
}

YSE::INTERNAL::global::global() : slowThreads(100, 1), fastThreads(2), update(false), active(false) {}

void YSE::INTERNAL::global::init() {
  REVERB::Manager().create();
}

void YSE::INTERNAL::global::close() {
  // first wait for all threads to exit
  slowThreads.shutdown();
  fastThreads.shutdown();
}
