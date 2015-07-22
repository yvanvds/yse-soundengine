/*
  ==============================================================================

    playerInterface.cpp
    Created: 9 Apr 2015 1:38:34pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "playerInterface.hpp"
#include "../music/scale/scaleInterface.hpp"
#include "../music/motif/motifInterface.hpp"

YSE::PLAYER::interfaceObject::interfaceObject() : pimpl(nullptr), _isPlaying(false) {}

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
  _isPlaying = true;
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::stop() {
  messageObject m;
  m.ID = PLAY;
  m.boolValue = false;
  pimpl->sendMessage(m);
  _isPlaying = false;
  return *this;
}

Bool YSE::PLAYER::interfaceObject::isPlaying() {
  return _isPlaying;
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
  m.floatPair[0] = static_cast<Flt>(target);
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::setScale(YSE::scale & scale, Flt time) {
  messageObject m;
  m.ID = SCALE;
  m.object.ptr = scale.pimpl;
  m.object.time = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::addMotif(YSE::motif & motif, UInt weight) {
  messageObject m;
  m.ID = ADD_MOTIF;
  m.object.ptr = motif.pimpl;
  m.object.time = static_cast<Flt>(weight);
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::removeMotif(YSE::motif & motif) {
  messageObject m;
  m.ID = REM_MOTIF;
  m.object.ptr = motif.pimpl;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::adjustMotifWeight(YSE::motif & motif, UInt weight) {
  messageObject m;
  m.ID = ADJUST_MOTIF;
  m.object.ptr = motif.pimpl;
  m.object.time = static_cast<Flt>(weight);
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::playPartialMotifs(Flt target, Flt time) {
  messageObject m;
  m.ID = PARTIAL_MOTIF;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::playMotifs(Flt target, Flt time) {
  messageObject m;
  m.ID = PLAY_MOTIF;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::PLAYER::interfaceObject & YSE::PLAYER::interfaceObject::fitMotifsToScale(Flt target, Flt time) {
  messageObject m;
  m.ID = MOTIF_FITS_SCALE;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}


