/*
  ==============================================================================

    time.cpp
    Created: 2 Feb 2014 12:03:16pm
    Author:  yvan

  ==============================================================================
*/

#include "time.h"

juce_ImplementSingleton(YSE::INTERNAL::time)

void YSE::INTERNAL::time::update() {
  // update time delta
  last = current;
  current = std::clock();
  d = (current - last) / static_cast<Flt>(CLOCKS_PER_SEC);
}

Flt YSE::INTERNAL::time::delta() {
  return d;
}