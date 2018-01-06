/*
  ==============================================================================

    synthManager.cpp
    Created: 6 Jul 2014 10:01:40pm
    Author:  yvan

  ==============================================================================
*/

//#include "synthManager.h"
//#include "../internalHeaders.h"
//
//YSE::SYNTH::managerObject & YSE::SYNTH::Manager() {
//  static managerObject m;
//  return m;
//}
//
//YSE::SYNTH::implementationObject * YSE::SYNTH::managerObject::addImplementation(YSE::SYNTH::interfaceObject * head) {
//  implementations.emplace_front(head);
//  return &implementations.front();
//}
//
//void YSE::SYNTH::managerObject::update() {
//  bool remove = false;
//  for (auto i = implementations.begin(); i != implementations.end(); ++i) {
//    if (!(*i).sync()) {
//      remove = true;
//    }
//  }
//
//  // I assume that removing a synth happens not very often. So it's
//  // faster to do a second run if this is the case, instead of updating
//  // 2 iterators all the time
//  if (remove) {
//    auto previous = implementations.before_begin();
//    for (auto i = implementations.begin(); i != implementations.end(); ++i) {
//      if (!(*i).hasInterface()) {
//        implementations.erase_after(previous);
//        return;
//      }
//      previous++;
//    }
//  }
//}