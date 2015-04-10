/*
  ==============================================================================

    playerInterface.cpp
    Created: 9 Apr 2015 1:38:34pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "playerInterface.hpp"
#include "../music/scale.hpp"

YSE::PLAYER::interfaceObject::interfaceObject() : pimpl(nullptr) {}

YSE::PLAYER::interfaceObject::~interfaceObject() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::create(synth & s) {
  assert(pimpl == nullptr);
  pimpl = PLAYER::Manager().addImplementation(this, &s);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::play() {
  messageObject m;
  m.ID = PLAY;
  m.boolValue = true;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::stop() {
  messageObject m;
  m.ID = PLAY;
  m.boolValue = false;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMinimumPitch(Flt target, Flt time) {
  Clamp(target, 0.f, 126.f);
  messageObject m;
  m.ID = MIN_PITCH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMaximumPitch(Flt target, Flt time) {
  Clamp(target, 1.f, 127.f);
  messageObject m;
  m.ID = MAX_PITCH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMinimumVelocity(Flt target, Flt time) {
  Clamp(target, 0.f, 0.999999f);
  messageObject m;
  m.ID = MIN_VELOCITY;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMaximumVelocity(Flt target, Flt time) {
  Clamp(target, 0.000001f, 1.f);
  messageObject m;
  m.ID = MAX_VELOCITY;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMinimumGap(Flt target, Flt time) {
  if(target < 0) target = 0;
  messageObject m;
  m.ID = MIN_GAP;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMaximumGap(Flt target, Flt time) {
  if (target < 0) target = 0;
  messageObject m;
  m.ID = MAX_GAP;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMinimumLength(Flt target, Flt time) {
  if (target < 0) target = 0;
  messageObject m;
  m.ID = MIN_LENGTH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setMaximumLength(Flt target, Flt time) {
  if (target < 0) target = 0;
  messageObject m;
  m.ID = MAX_LENGTH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setVoices(UInt target, Flt time) {
  messageObject m;
  m.ID = VOICES;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setScale(MUSIC::scale & scale, Flt time) {
  messageObject m;
  m.ID = SCALE;
  m.object.ptr = &scale;
  m.object.time = time;
  pimpl->sendMessage(m);
  return *this;
}

