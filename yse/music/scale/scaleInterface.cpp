/*
  ==============================================================================

    scaleInterface.cpp
    Created: 14 Apr 2015 2:54:43pm
    Author:  yvan

  ==============================================================================
*/

#include "../../internalHeaders.h"
#include "scaleInterface.hpp"
#include <algorithm>

YSE::SCALE::interfaceObject::interfaceObject() {
  pimpl = SCALE::Manager().addImplementation(this);
}

YSE::SCALE::interfaceObject::~interfaceObject() {
  pimpl->removeInterface();
}

YSE::scale & YSE::SCALE::interfaceObject::add(Flt pitch, Flt step) {
  if (step > 0) {
    // find lowest occurance of this pitch
    while (pitch + step > 0) {
      pitch -= step;
    }

    // add pitches to scale
    while (pitch < 128) {
      pitches.push_back(pitch);
      pitch += step;
    }
  }
  else {
    pitches.push_back(pitch);
  }

  // sort and delete doubles
  std::sort(pitches.begin(), pitches.end());
  pitches.erase(unique(pitches.begin(), pitches.end()), pitches.end());

  messageObject m;
  m.ID = ADD;
  m.floatPair[0] = pitch;
  m.floatPair[1] = step;
  pimpl->sendMessage(m);
  return *this;
}

YSE::scale & YSE::SCALE::interfaceObject::remove(Flt pitch, Flt step) {
  if (step > 0) {
    // find lowest occurance of this pitch
    while (pitch + step > 0) {
      pitch -= step;
    }

    // remove pitches from scale
    for (auto iter = pitches.begin(); iter != pitches.end();) {
      if ((*iter) > pitch) pitch += step;
      if ((*iter) == pitch) {
        iter = pitches.erase(iter);
      }
      else {
        ++iter;
      }
    }
  }
  else {
    // remove pitch from scale
    for (auto iter = pitches.begin(); iter != pitches.end();) {
      if ((*iter) == pitch) {
        iter = pitches.erase(iter);
      }
      else {
        ++iter;
      }
    }
  }

  messageObject m;
  m.ID = REMOVE;
  m.floatPair[0] = pitch;
  m.floatPair[1] = step;
  pimpl->sendMessage(m);
  return *this;
}

Bool YSE::SCALE::interfaceObject::has(Flt pitch) {
  return std::binary_search(pitches.begin(), pitches.end(), pitch);
}

Flt YSE::SCALE::interfaceObject::getNearest(Flt pitch) const {
  std::vector<Flt>::const_iterator low, high;
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

UInt YSE::SCALE::interfaceObject::size() const {
  return pitches.size();
}

YSE::scale & YSE::SCALE::interfaceObject::clear() {
  pitches.clear();
  messageObject m;
  m.ID = CLEAR;
  pimpl->sendMessage(m);
  return *this;
}



YSE::scale & YSE::SCALE::interfaceObject::operator=(const scale & other) {
  clear();

  FOREACH(other.pitches) {
    add(other.pitches[i], 0); // don't add transpositions
  }

  return *this;
}

YSE::SCALE::interfaceObject::interfaceObject(const scale & other) {
  pimpl = SCALE::Manager().addImplementation(this);
  *this = other;
}