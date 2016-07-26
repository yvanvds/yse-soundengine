/*
  ==============================================================================

    motifManager.cpp
    Created: 14 Apr 2015 6:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "../../internalHeaders.h"

YSE::MOTIF::managerObject & YSE::MOTIF::Manager() {
  static managerObject m;
  return m;
}

YSE::MOTIF::managerObject::managerObject() {}

YSE::MOTIF::managerObject::~managerObject() {
  implementations.clear();
}

void YSE::MOTIF::managerObject::update() {
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

YSE::MOTIF::implementationObject * YSE::MOTIF::managerObject::addImplementation(motif * head) {
  implementations.emplace_front(head);
  return &implementations.front();
}

