/*
  ==============================================================================

    scale.cpp
    Created: 10 Apr 2015 5:14:25pm
    Author:  yvan

  ==============================================================================
*/

#include "scale.hpp"
#include <algorithm>

YSE::MUSIC::scale & YSE::MUSIC::scale::add(Flt pitch, Flt step) {
  // find lowest occurance of this pitch
  while (pitch > 0) {
    pitch -= step;
  }

  // add pitches to scale
  while (pitch < 128) {
    pitches.push_back(pitch);
    pitch += step;
  }

  return *this;
}

YSE::MUSIC::scale & YSE::MUSIC::scale::sort() {
  std::sort(pitches.begin(), pitches.end());
  pitches.erase(unique(pitches.begin(), pitches.end()), pitches.end());
  return *this;
}

Bool YSE::MUSIC::scale::has(Flt pitch) {
  return std::binary_search(pitches.begin(), pitches.end(), pitch);
}
 
Flt YSE::MUSIC::scale::getNearest(Flt pitch) {
  std::vector<Flt>::iterator low, high;
  high = std::lower_bound(pitches.begin(), pitches.end(), pitch);
  // lower bound might be equal to pitch, in which case we just return pitch
  if (*high == pitch) return pitch;

  // if this points to the first element, return this value
  if (high == pitches.begin()) return *high;
  
  // if not, find the pitch below 
  low = high;
  low--;
  return (pitch - *low < *high - pitch) ? *low : *high;
}
