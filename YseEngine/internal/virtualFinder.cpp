/*
  ==============================================================================

    virtualFinder.cpp
    Created: 8 Mar 2014 8:13:39pm
    Author:  yvan

  ==============================================================================
*/

#include "virtualFinder.h"
#include <cstdlib>

YSE::virtualFinder& YSE::VirtualSoundFinder() {
  static virtualFinder vf(10);
  return vf;
}

YSE::virtualFinder::virtualFinder(Int resolution)
  : resolution(resolution), limit(50), maximum(0.f), calculatedMax(10.f) {
  // Clamp the requested initial resolution into the valid band and allocate the
  // backing histogram once at its maximum size. It is never resized afterwards
  // (issue #194): calculate() only adjusts the logical `resolution`.
  if (this->resolution < RESOLUTION_MIN) this->resolution = RESOLUTION_MIN;
  if (this->resolution > RESOLUTION_MAX) this->resolution = RESOLUTION_MAX;
  bin.resize(RESOLUTION_MAX, 0);
}

void YSE::virtualFinder::setLimit(Int value) {
  this->limit.store(value);
}

Int YSE::virtualFinder::getLimit() {
  return this->limit;
}

void YSE::virtualFinder::reset() {
  if (maximum > 0)
    calculatedMax = maximum;
  else
    calculatedMax = 10;

  for (Int i = 0; i < resolution; i++)
    bin[i] = 0;
  maximum = 0;
  entries = 0;
}

void YSE::virtualFinder::add(Flt value) {
  entries++;
  if (value > maximum) maximum = value;
  // now be sure not to divide by zero
  Int sort;
  if (calculatedMax == 0)
    sort = 0;
  else
    sort = static_cast<Int>(value / (calculatedMax / resolution));
  if (sort > resolution - 1) {
    sort = resolution - 1;
  }
  if (sort < 0) {
    sort = 0;
  }
  bin[sort]++;
}

bool YSE::virtualFinder::inRange(Flt value, bool wasReal) {
  if (entries < currentLimit) return true;
  // Hysteresis (issue #206): a sound already real holds on until it drifts ~5%
  // past the cutoff, while a virtual sound must come ~5% inside the cutoff
  // before it is promoted back. The ~10% gap between the two thresholds keeps a
  // sound hovering at the boundary from toggling every tick as `range` wobbles.
  Flt threshold = wasReal ? range * 1.05f : range * 0.95f;
  return value < threshold;
}

void YSE::virtualFinder::calculate() {
  // add up bins until we have enough entries
  Int count = 0;
  currentLimit = limit.load();
  Int i;
  for (i = 0; i < resolution; i++) {
    count += bin[i];
    if (count > currentLimit) {
      break;
    }
  }

  if (count > currentLimit) {
    range = calculatedMax / resolution * (i + 1);
    range -= calculatedMax / resolution * (1 - (count - currentLimit) / bin[i]);

    // if the difference between the count and the limit is more than 10%, we have to enlarge our
    // resolution; if it is very small we can shrink it. The backing buffer is never resized — we
    // only move the logical `resolution` within [RESOLUTION_MIN, RESOLUTION_MAX] (issue #194).
    if (abs(count - currentLimit) > currentLimit / 10.f) {
      if (resolution < RESOLUTION_MAX) resolution++;
    } else if (abs(count - currentLimit) < currentLimit / 50.f) {
      if (resolution > RESOLUTION_MIN) resolution--;
    }
  } else {
    range = maximum;
  }
}
