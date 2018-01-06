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

YSE::player::player() : pimpl(nullptr), _isPlaying(false) {}

YSE::player::~player() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

/*YSE::player & YSE::player::create(synth & s) {
  assert(pimpl == nullptr);
  pimpl = PLAYER::Manager().addImplementation(this, &s);
  return *this;
}*/

YSE::player & YSE::player::play() {
  PLAYER::messageObject m;
  m.ID = PLAYER::PLAY;
  m.boolValue = true;
  pimpl->sendMessage(m);
  _isPlaying = true;
  return *this;
}

YSE::player & YSE::player::stop() {
  PLAYER::messageObject m;
  m.ID = PLAYER::PLAY;
  m.boolValue = false;
  pimpl->sendMessage(m);
  _isPlaying = false;
  return *this;
}

Bool YSE::player::isPlaying() {
  return _isPlaying;
}

YSE::player & YSE::player::setMinimumPitch(Flt target, Flt time) {
  Clamp(target, 0.f, 126.f);
  PLAYER::messageObject m;
  m.ID = PLAYER::MIN_PITCH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMaximumPitch(Flt target, Flt time) {
  Clamp(target, 1.f, 127.f);
  PLAYER::messageObject m;
  m.ID = PLAYER::MAX_PITCH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMinimumVelocity(Flt target, Flt time) {
  Clamp(target, 0.f, 0.999999f);
  PLAYER::messageObject m;
  m.ID = PLAYER::MIN_VELOCITY;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMaximumVelocity(Flt target, Flt time) {
  Clamp(target, 0.000001f, 1.f);
  PLAYER::messageObject m;
  m.ID = PLAYER::MAX_VELOCITY;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMinimumGap(Flt target, Flt time) {
  if(target < 0) target = 0;
  PLAYER::messageObject m;
  m.ID = PLAYER::MIN_GAP;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMaximumGap(Flt target, Flt time) {
  if (target < 0) target = 0;
  PLAYER::messageObject m;
  m.ID = PLAYER::MAX_GAP;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMinimumLength(Flt target, Flt time) {
  if (target < 0) target = 0;
  PLAYER::messageObject m;
  m.ID = PLAYER::MIN_LENGTH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setMaximumLength(Flt target, Flt time) {
  if (target < 0) target = 0;
  PLAYER::messageObject m;
  m.ID = PLAYER::MAX_LENGTH;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setVoices(UInt target, Flt time) {
  PLAYER::messageObject m;
  m.ID = PLAYER::VOICES;
  m.floatPair[0] = static_cast<Flt>(target);
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::setScale(YSE::scale & scale, Flt time) {
  PLAYER::messageObject m;
  m.ID = PLAYER::SCALE;
  m.object.ptr = scale.pimpl;
  m.object.time = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::addMotif(YSE::motif & motif, UInt weight) {
  PLAYER::messageObject m;
  m.ID = PLAYER::ADD_MOTIF;
  m.object.ptr = motif.pimpl;
  m.object.time = static_cast<Flt>(weight);
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::removeMotif(YSE::motif & motif) {
  PLAYER::messageObject m;
  m.ID = PLAYER::REM_MOTIF;
  m.object.ptr = motif.pimpl;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::adjustMotifWeight(YSE::motif & motif, UInt weight) {
  PLAYER::messageObject m;
  m.ID = PLAYER::ADJUST_MOTIF;
  m.object.ptr = motif.pimpl;
  m.object.time = static_cast<Flt>(weight);
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::playPartialMotifs(Flt target, Flt time) {
  PLAYER::messageObject m;
  m.ID = PLAYER::PARTIAL_MOTIF;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::playMotifs(Flt target, Flt time) {
  PLAYER::messageObject m;
  m.ID = PLAYER::PLAY_MOTIF;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}

YSE::player & YSE::player::fitMotifsToScale(Flt target, Flt time) {
  PLAYER::messageObject m;
  m.ID = PLAYER::MOTIF_FITS_SCALE;
  m.floatPair[0] = target;
  m.floatPair[1] = time;
  pimpl->sendMessage(m);
  return *this;
}


