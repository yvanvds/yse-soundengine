/*
  ==============================================================================

    scaleImplementation.cpp
    Created: 14 Apr 2015 2:54:18pm
    Author:  yvan

  ==============================================================================
*/

#include "scale.hpp"
#include "../../internalHeaders.h"
#include <algorithm>

YSE::SCALE::implementationObject::implementationObject(scale * head)
: head(head) {}

YSE::SCALE::implementationObject::~implementationObject() {
  if (head.load() != nullptr) {
    head.load()->pimpl = nullptr;
  }
}

bool YSE::SCALE::implementationObject::update() {
  if (head.load() == nullptr) return false;

  // parse messages
  needsSorting = false;
  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }

  if (needsSorting) {
    // sort and delete doubles
    std::sort(pitches.begin(), pitches.end());
    pitches.erase(unique(pitches.begin(), pitches.end()), pitches.end());
  }
  return true;
}

void YSE::SCALE::implementationObject::removeInterface() {
  head.store(nullptr);
}

void YSE::SCALE::implementationObject::parseMessage(const messageObject & message) {
  switch (message.ID) {
  case ADD:
    add(message.floatPair[0], message.floatPair[1]);
    break;
  case REMOVE:
    remove(message.floatPair[0], message.floatPair[1]);
    break;
  case CLEAR:
    clear();
    break;
  }
}

void YSE::SCALE::implementationObject::add(Flt pitch, Flt step) {
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

  needsSorting = true;
}

void YSE::SCALE::implementationObject::remove(Flt pitch, Flt step) {
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
}

Bool YSE::SCALE::implementationObject::has(Flt pitch) {
  return std::binary_search(pitches.begin(), pitches.end(), pitch);
}

Flt YSE::SCALE::implementationObject::getNearest(Flt pitch) const {
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

UInt YSE::SCALE::implementationObject::size() const {
  return pitches.size();
}

void YSE::SCALE::implementationObject::clear() {
  pitches.clear();
}