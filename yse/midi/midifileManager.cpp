/*
  ==============================================================================

    midifileManager.cpp
    Created: 13 Jul 2014 5:21:20pm
    Author:  yvan

  ==============================================================================
*/

#include "midifileManager.h"
#include "../internalHeaders.h"

YSE::MIDI::managerObject & YSE::MIDI::Manager() {
  static managerObject m;
  return m;
}

YSE::MIDI::fileImpl * YSE::MIDI::managerObject::addImplementation(YSE::MIDI::file * head) {
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::MIDI::managerObject::update() {
  auto previous = implementations.before_begin();
  for (auto i = implementations.begin(); i != implementations.end(); ++i) {
    if (!(*i).hasInterface()) {
      implementations.erase_after(previous);
      return;
    }
    previous++;
  }
}

