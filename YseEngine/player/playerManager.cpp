/*
  ==============================================================================

    playerManager.cpp
    Created: 9 Apr 2015 1:39:02pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::PLAYER::managerObject & YSE::PLAYER::Manager() {
  static managerObject m;
  return m;
}

YSE::PLAYER::managerObject::managerObject() {}

YSE::PLAYER::managerObject::~managerObject() {
  implementations.clear();
}

void YSE::PLAYER::managerObject::update(Flt delta) {
  auto previous = implementations.before_begin();
  for (auto i = implementations.begin(); i != implementations.end(); ) {
    if (!i->update(delta)) {
      i = implementations.erase_after(previous);
    }
    previous = i;
    ++i;
  }
}

/*YSE::PLAYER::implementationObject * YSE::PLAYER::managerObject::addImplementation(player * head, synth * s) {
  implementations.emplace_front(head, s);
  return &implementations.front();
}*/



