/*
  ==============================================================================

    listenerImplementation.cpp
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"

YSE::INTERNAL::listenerImplementation & YSE::INTERNAL::ListenerImpl() {
  static listenerImplementation impl;
  return impl;
}

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

void YSE::INTERNAL::listenerImplementation::update() {
  // ugly, but the atomic version of vector isn't great right now
  newPos.x = pos.x.load() * (Settings().distanceFactor);
  newPos.y = pos.y.load() * (Settings().distanceFactor);
  newPos.z = pos.z.load() * (Settings().distanceFactor);
  vel.x.store((newPos.x - lastPos.x) * (1.f / Time().delta()));
  vel.y.store((newPos.y - lastPos.y) * (1.f / Time().delta()));
  vel.z.store((newPos.z - lastPos.z) * (1.f / Time().delta()));
  lastPos = newPos;
}
