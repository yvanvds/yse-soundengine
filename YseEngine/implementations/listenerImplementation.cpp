/*
  ==============================================================================

    listenerImplementation.cpp
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::INTERNAL::listenerImplementation& YSE::INTERNAL::ListenerImpl() {
  static listenerImplementation impl;
  return impl;
}

YSE::INTERNAL::listenerImplementation::listenerImplementation() {
  newPos.zero();
  lastPos.zero();
  vel.store(0.f, 0.f, 0.f);
  forward.store(0.f, 0.f, 0.f);
  up.store(0.f, 1.f, 0.f);
  pos.store(0.f, 0.f, 0.f);
}

void YSE::INTERNAL::listenerImplementation::update() {
  // Read the control-thread-written position as one tear-free snapshot, then
  // publish the derived velocity as one snapshot too (seqlock, issue #196).
  const Pos p = pos.load();
  newPos.x = p.x * (Settings().distanceFactor);
  newPos.y = p.y * (Settings().distanceFactor);
  newPos.z = p.z * (Settings().distanceFactor);
  const Flt invDelta = 1.f / Time().delta();
  vel.store((newPos.x - lastPos.x) * invDelta, (newPos.y - lastPos.y) * invDelta,
            (newPos.z - lastPos.z) * invDelta);
  lastPos = newPos;
}
