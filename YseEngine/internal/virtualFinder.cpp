/*
  ==============================================================================

    virtualFinder.cpp
    Created: 8 Mar 2014 8:13:39pm
    Author:  yvan

  ==============================================================================
*/

#include "virtualFinder.h"
#include <cstdlib>

YSE::virtualFinder & YSE::VirtualSoundFinder() {
  static virtualFinder vf(10);
  return vf;
}

YSE::virtualFinder::virtualFinder(Int resolution) : resolution(resolution), limit(50), maximum(0.f), calculatedMax(10.f) {
  bin.resize(resolution, 0);
}

void YSE::virtualFinder::setLimit(Int value) {
  this->limit.store(value);
}

Int YSE::virtualFinder::getLimit() {
  return this->limit;
}

void YSE::virtualFinder::reset() {
  if (maximum > 0) calculatedMax = maximum;
  else calculatedMax = 10;

  for (UInt i = 0; i < bin.size(); i++) bin[i] = 0;
  maximum = 0;
  entries = 0;
}

void YSE::virtualFinder::add(Flt value) {
  entries++;
  if (value > maximum) maximum = value;
  // now be sure not to divide by zero
  Int sort;
  if (calculatedMax == 0) sort = 0;
  else sort = static_cast<Int>(value / (calculatedMax / resolution));
  if ((UInt)sort > bin.size() - 1) {
    sort = bin.size() - 1;
  }
  bin[sort]++;
}

bool YSE::virtualFinder::inRange(Flt value) {
  if (entries < currentLimit) return true;
  if (value < range) return true;
  return false;
}

void YSE::virtualFinder::calculate() {
  // add up bins until we have enough entries
  Int count = 0;
  currentLimit = limit.load();
  UInt i;
  for (i = 0; i < bin.size(); i++) {
    count += bin[i];
    if (count > currentLimit) {
      break;
    }
  }

  if (count > currentLimit) {
    range = calculatedMax / resolution * (i+1);
    range -= calculatedMax / resolution * (1 - (count - currentLimit) / bin[i]);

    // if the difference between the count and the limit is more than 10%, we have to enlarge our resolution
      if (abs(count - currentLimit) > currentLimit / 10.f) {
      resolution++;
      bin.resize(resolution);
    }
    else if (abs(count - currentLimit) < currentLimit / 50.f) {
      resolution--;
      bin.resize(resolution);

    }
  }
  else {
    range = maximum;
  }


}

