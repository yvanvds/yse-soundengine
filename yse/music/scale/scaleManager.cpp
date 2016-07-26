/*
  ==============================================================================

    scaleManager.cpp
    Created: 14 Apr 2015 2:55:05pm
    Author:  yvan

  ==============================================================================
*/

#include "../../internalHeaders.h"

YSE::SCALE::managerObject & YSE::SCALE::Manager() {
  static managerObject m;
  return m;
}

YSE::SCALE::managerObject::managerObject() {}

YSE::SCALE::managerObject::~managerObject() {
  implementations.clear();
}

void YSE::SCALE::managerObject::update() {
  auto previous = implementations.before_begin();
  for (auto i = implementations.begin(); i != implementations.end();) {
    if (!i->update()) {
      i = implementations.erase_after(previous);
    }
    else {
      previous = i;
      ++i;
    }
  }
}

YSE::SCALE::implementationObject * YSE::SCALE::managerObject::addImplementation(scale * head) {
  implementations.emplace_front(head);
  return &implementations.front();
}
