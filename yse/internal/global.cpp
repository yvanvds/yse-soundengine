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

void YSE::INTERNAL::global::init() {
  dm = deviceManager::getInstance();
  sm = soundManager::getInstance();
  log = logImplementation::getInstance();
  ysetime = time::getInstance();
  set = settings::getInstance();
  cm = channelManager::getInstance();
  li = listenerImplementation::getInstance();
  rm = reverbManager::getInstance();
}

void YSE::INTERNAL::global::close() {
  channelManager::deleteInstance();
  listenerImplementation::deleteInstance();
  reverbManager::deleteInstance();
  soundManager::deleteInstance();
  // these have to come last!
  deviceManager::deleteInstance();
  settings::deleteInstance();
  logImplementation::deleteInstance();
}
