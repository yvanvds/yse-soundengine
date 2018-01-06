/*
  ==============================================================================

    time.cpp
    Created: 2 Feb 2014 12:03:16pm
    Author:  yvan

  ==============================================================================
*/

#include "time.h"

YSE::INTERNAL::time & YSE::INTERNAL::Time() {
  static time t;
  return t;
}

YSE::INTERNAL::time & YSE::INTERNAL::DeviceTime() {
  static time t;
  return t;
}

void YSE::INTERNAL::time::update() {
  // update time delta
  last = current;
  current = std::clock();
  d = (current - last) / static_cast<Flt>(CLOCKS_PER_SEC);
}

Flt YSE::INTERNAL::time::delta() {
  return d;
}