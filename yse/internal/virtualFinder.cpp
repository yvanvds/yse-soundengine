/*
  ==============================================================================

    virtualFinder.cpp
    Created: 8 Mar 2014 8:13:39pm
    Author:  yvan

  ==============================================================================
*/

#include "virtualFinder.h"
#include "JuceHeader.h"
#include <cmath>

YSE::virtualFinder::virtualFinder(Int resolution) : resolution(resolution), maximum(0.f), calculatedMax(10.f) {
  jassertfalse(resolution == 0);
  bin.resize(resolution, 0);
}

void YSE::virtualFinder::setLimit(Int value) {
  this->limit = limit;
}

void YSE::virtualFinder::reset() {
  if (maximum > 0) calculatedMax = maximum;
  else calculatedMax = 10;

  for (auto d : bin) d = 0;
  maximum = 0;
  entries = 0;
}

void YSE::virtualFinder::add(Flt value) {
  entries++;
  if (value > maximum) maximum = value;
  // now be sure not to divide by zero
  Int sort;
  if (calculatedMax == 0) sort = 0;
  else sort = static_cast<Int>(value / calculatedMax * resolution);
  bin[sort]++;
}

bool YSE::virtualFinder::inRange(Flt value) {
  if (entries < limit) return true;
  if (value < range) return true;
  return false;
}

void YSE::virtualFinder::calculate() {
  // add up bins until we have enough entries
  Int count;
  for (auto d : bin) {
    count += d;
    if (count > limit) {
      // too many sounds, see if we have to go one step back
      if ((count - limit) > (limit - (range - d))) count -= d;
      break;
    }
  }
  range = count * maximum;

  // if the difference between the count and the limit is more than 10%, we have to enlarge our resolution
  if (std::abs(count - limit) > limit / 10.f) {
    resolution++;
  }
  else if (std::abs(count - limit) < limit / 50.f) {
    resolution--;
  }
}

