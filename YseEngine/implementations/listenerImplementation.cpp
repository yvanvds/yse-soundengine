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
  // Same zero-tick rule the sound path applies to its own velocity
  // (DSP::panner::computeVelocity, issue #660): a tick that measured no time
  // reports delta == 0 — the clock's seeding tick, or two update ticks inside
  // one clock period. Dividing by that is +inf, and for a *stationary*
  // listener 0 * inf is NaN — which every sound then loads as listenerVelocity
  // and pushes through computeDopplerRatio, latching its playhead at NaN. A
  // tick that measured no time carries no velocity information, so the last
  // published velocity stands. The condition is written to also reject a NaN
  // delta, which an ordinary `delta != 0` would let through.
  const Flt delta = Time().delta();
  if (delta > 0.f) {
    const Flt invDelta = 1.f / delta;
    vel.store((newPos.x - lastPos.x) * invDelta, (newPos.y - lastPos.y) * invDelta,
              (newPos.z - lastPos.z) * invDelta);
  }
  lastPos = newPos;
}
