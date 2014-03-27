/*
  ==============================================================================

    listenerImplementation.cpp
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"


juce_ImplementSingleton(YSE::INTERNAL::listenerImplementation)

YSE::INTERNAL::listenerImplementation::listenerImplementation() {
  newPos.zero();
  lastPos.zero();
  vel.x.store(0.f);
  vel.y.store(0.f);
  vel.z.store(0.f);
  forward.x.store(0.f);
  forward.y.store(0.f);
  forward.z.store(0.f);
  up.x.store(0.f);
  up.y.store(1.f);
  up.z.store(0.f);
  pos.x.store(0.f);
  pos.y.store(0.f);
  pos.z.store(0.f);
}

YSE::INTERNAL::listenerImplementation::~listenerImplementation() {
  clearSingletonInstance();
}

void YSE::INTERNAL::listenerImplementation::update() {
  // ugly, but the atomic version of vector isn't great right now
  newPos.x = pos.x.load() * (Global.getSettings().distanceFactor);
  newPos.y = pos.y.load() * (Global.getSettings().distanceFactor);
  newPos.z = pos.z.load() * (Global.getSettings().distanceFactor);
  vel.x.store((newPos.x - lastPos.x) * (1.f / Global.getTime().delta()));
  vel.y.store((newPos.y - lastPos.y) * (1.f / Global.getTime().delta()));
  vel.z.store((newPos.z - lastPos.z) * (1.f / Global.getTime().delta()));
  lastPos = newPos;
}
