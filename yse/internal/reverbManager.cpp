/*
  ==============================================================================

    reverbManager.cpp
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#include "reverbManager.h"
#include "../implementations/channelImplementation.h"
#include "../reverb.hpp"

juce_ImplementSingleton(YSE::INTERNAL::reverbManager)

YSE::INTERNAL::reverbManager::reverbManager() {
  reverbObject = reverbDSP::getInstance();
}

YSE::INTERNAL::reverbManager::~reverbManager() {
  // this is important because we don't want reverbs to 
  // unregister themselves when the manager is gone.
  for (Int i = 0; i < reverbs.size(); i++) {
    reverbs.getUnchecked(i)->connectedToManager = false;
  }
  reverbDSP::deleteInstance();
  clearSingletonInstance();
}

void YSE::INTERNAL::reverbManager::add(reverb * r) {
  //ScopedLock lock(reverbs.getLock());
  reverbs.add(r);
  r->connectedToManager = false;
}

void YSE::INTERNAL::reverbManager::remove(reverb * r) {
  //ScopedLock lock(reverbs.getLock());
  reverbs.removeFirstMatchingValue(r);
  r->connectedToManager = false;
}
 
void YSE::INTERNAL::reverbManager::attachToChannel(YSE::INTERNAL::channelImplementation * ptr) {
  ScopedLock  lock(reverbDSPLock);
  reverbChannel = ptr;
}

void YSE::INTERNAL::reverbManager::process(YSE::INTERNAL::channelImplementation * ptr) {
  if (ptr != reverbChannel) return;

  ScopedLock lock(reverbDSPLock);
  reverbObject->process(ptr->out);
}