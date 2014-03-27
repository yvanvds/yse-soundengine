/*
  ==============================================================================

    global.cpp
    Created: 27 Jan 2014 10:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "global.h"
#include "time.h"
#include "settings.h"
#include "../implementations/listenerImplementation.h"


YSE::INTERNAL::global YSE::INTERNAL::Global;

YSE::INTERNAL::deviceManager & YSE::INTERNAL::global::getDeviceManager() {
  return *dm;
}

YSE::SOUND::managerObject & YSE::INTERNAL::global::getSoundManager() {
  return *sm;
}

YSE::INTERNAL::logImplementation & YSE::INTERNAL::global::getLog() {
  return *log;
}

YSE::INTERNAL::time & YSE::INTERNAL::global::getTime() {
  return *ysetime;
}

YSE::INTERNAL::settings & YSE::INTERNAL::global::getSettings() {
  return *set;
}

YSE::CHANNEL::managerObject & YSE::INTERNAL::global::getChannelManager() {
  return *cm;
}

YSE::INTERNAL::listenerImplementation & YSE::INTERNAL::global::getListener() {
  return *li;
}

YSE::REVERB::managerObject & YSE::INTERNAL::global::getReverbManager() {
  return *rm;
}

void YSE::INTERNAL::global::addSlowJob(ThreadPoolJob * job) {
  slowThreads.addJob(job, false);
}

void YSE::INTERNAL::global::addFastJob(ThreadPoolJob * job) {
  fastThreads.addJob(job, false);
}

void YSE::INTERNAL::global::waitForFastJob(ThreadPoolJob * job) {
  if (fastThreads.contains(job)) {
    fastThreads.waitForJobToFinish(job, -1);
  }
}

void YSE::INTERNAL::global::waitForSlowJob(ThreadPoolJob * job) {
  if (slowThreads.contains(job)) {
    slowThreads.waitForJobToFinish(job, -1);
  }
}

bool YSE::INTERNAL::global::containsSlowJob(ThreadPoolJob * job) {
  return slowThreads.contains(job);
}

YSE::INTERNAL::global::global() : slowThreads(1), update(false), active(false) {}

void YSE::INTERNAL::global::init() {
  dm = deviceManager::getInstance();
  sm = SOUND::managerObject::getInstance();
  log = logImplementation::getInstance();
  ysetime = time::getInstance();
  set = settings::getInstance();
  cm = CHANNEL::managerObject::getInstance();
  li = listenerImplementation::getInstance();
  rm = REVERB::managerObject::getInstance();

  slowThreads.setThreadPriorities(0);
  fastThreads.setThreadPriorities(10);

  rm->create();
  cm->create();
  sm->create();
}

void YSE::INTERNAL::global::close() {
  // first wait for all threads to exit
  slowThreads.removeAllJobs(true, -1);
  fastThreads.removeAllJobs(true, -1);

  // remove managers
  deviceManager::deleteInstance();
  SOUND::managerObject::deleteInstance();
  CHANNEL::managerObject::deleteInstance();
  listenerImplementation::deleteInstance();
  REVERB::managerObject::deleteInstance();
  
  // these have to come last!
  settings::deleteInstance();
  logImplementation::deleteInstance();
}
