/*
  ==============================================================================

    interpolators.cpp
    Created: 10 Apr 2015 8:07:38pm
    Author:  yvan

  ==============================================================================
*/

#include "interpolators.hpp"

YSE::linearInterpolator::linearInterpolator()
: startValue(0), targetValue(0), currentValue(0), totalTime(0), timeLeft(0) {}

YSE::linearInterpolator & YSE::linearInterpolator::set(Flt target, Flt time) {
  startValue = currentValue;
  targetValue = target;
  totalTime = timeLeft = time;
  if (timeLeft == 0) {
    currentValue = targetValue;
  }
  return *this;
}

Flt YSE::linearInterpolator::target() {
  return targetValue;
}

Flt YSE::linearInterpolator::operator()() {
  return currentValue;
}

YSE::linearInterpolator & YSE::linearInterpolator::update(Flt timeDelta) {
  if (timeLeft > 0) {
    timeLeft -= timeDelta;
    if (timeLeft < 0) timeLeft = 0;
    if (timeLeft == 0) {
      currentValue = targetValue;
    }
    else {
      currentValue = startValue + ((targetValue - startValue) * (1 - (timeLeft / totalTime)));
    }
  }
  return *this;
}

