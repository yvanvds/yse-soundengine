/*
  ==============================================================================

    global.cpp
    Created: 27 Jan 2014 10:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "global.h"
#include "deviceManager.h"
#include "soundManager.h"
#include "../implementations/logImplementation.h"
#include "../implementations/listenerImplementation.h"
#include "../implementations/channelImplementation.h"
#include "time.h"
#include "settings.h"
#include "channelManager.h"
#include "reverbManager.h"

YSE::INTERNAL::global YSE::INTERNAL::Global;

YSE::INTERNAL::deviceManager & YSE::INTERNAL::global::getDeviceManager() {
  return *dm;
}

YSE::INTERNAL::soundManager & YSE::INTERNAL::global::getSoundManager() {
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

YSE::INTERNAL::channelManager & YSE::INTERNAL::global::getChannelManager() {
  return *cm;
}

YSE::INTERNAL::listenerImplementation & YSE::INTERNAL::global::getListener() {
  return *li;
}

YSE::INTERNAL::reverbManager & YSE::INTERNAL::global::getReverbManager() {
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
  sm = soundManager::getInstance();
  log = logImplementation::getInstance();
  ysetime = time::getInstance();
  set = settings::getInstance();
  cm = channelManager::getInstance();
  li = listenerImplementation::getInstance();
  rm = reverbManager::getInstance();

  slowThreads.setThreadPriorities(0);
  fastThreads.setThreadPriorities(10);
}

void YSE::INTERNAL::global::close() {
  // first wait for all threads to exit
  slowThreads.removeAllJobs(true, -1);
  fastThreads.removeAllJobs(true, -1);

  // remove managers
  deviceManager::deleteInstance();
  soundManager::deleteInstance();
  channelManager::deleteInstance();
  listenerImplementation::deleteInstance();
  reverbManager::deleteInstance();
  
  // these have to come last!
  settings::deleteInstance();
  logImplementation::deleteInstance();


}
