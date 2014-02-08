/*
  ==============================================================================

    listenerImplementation.cpp
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/

#include "listenerImplementation.h"
#include "../internal/global.h"
#include "../internal/settings.h"
#include "../internal/time.h"


juce_ImplementSingleton(YSE::INTERNAL::listenerImplementation)

YSE::INTERNAL::listenerImplementation::listenerImplementation() {
  newPos.zero();
  lastPos.zero();
  vel.x.set(0.f);
  vel.y.set(0.f);
  vel.z.set(0.f);
  forward.x.set(0.f);
  forward.y.set(0.f);
  forward.z.set(1.f);
  up.x.set(0.f);
  up.y.set(1.f);
  up.z.set(0.f);
  pos.x.set(0.f);
  pos.y.set(0.f);
  pos.z.set(0.f);
}

YSE::INTERNAL::listenerImplementation::~listenerImplementation() {
  clearSingletonInstance();
}

void YSE::INTERNAL::listenerImplementation::update() {
  // ugly, but the atomic version of vector isn't great right now
  newPos.x = pos.x.get() * (Global.getSettings().distanceFactor);
  newPos.y = pos.y.get() * (Global.getSettings().distanceFactor);
  newPos.z = pos.z.get() * (Global.getSettings().distanceFactor);
  vel.x.set((newPos.x - lastPos.x) * (1.f / Global.getTime().delta()));
  vel.y.set((newPos.y - lastPos.y) * (1.f / Global.getTime().delta()));
  vel.z.set((newPos.z - lastPos.z) * (1.f / Global.getTime().delta()));
  lastPos = newPos;
}
