/*
  ==============================================================================

    reverb.cpp
    Created: 1 Feb 2014 7:02:58pm
    Author:  yvan

  ==============================================================================
*/

#include "reverb.hpp"
#include "internal/global.h"
#include "utils/misc.hpp"
#include "internal/global.h"
#include "internal/reverbManager.h"

YSE::reverb & YSE::GlobalReverb() {
  static reverb r;
  return r;
}

YSE::reverb::reverb() : _active(true), _roomsize(0.5f), _damp(0.5),  
                        _wet(0.5), _dry(0.5), _modFrequency(0),
                        _modWidth(0), global(false), connectedToManager(false) {

  for (Int i = 0; i < 4; i++) {
    _earlyPtr[i] = 0;
    _earlyGain[i] = 0;
  }

  INTERNAL::Global.getReverbManager().add(this);
}


YSE::reverb& YSE::reverb::release() {
  if (connectedToManager) {
    INTERNAL::Global.getReverbManager().remove(this);
  }
  return *this;
}

YSE::reverb::~reverb() {
  release();
}

YSE::reverb& YSE::reverb::pos(const Vec &value) {
  _position = value;
  return *this;
}

YSE::Vec YSE::reverb::pos() {
  return _position;
}

YSE::reverb& YSE::reverb::size(Flt value) {
  if (value < 0) value = 0;
  _size = value;
  return (*this);
}

Flt YSE::reverb::size() {
  return _size;
}

YSE::reverb& YSE::reverb::rolloff(Flt value) {
  if (value < 0) value = 0;
  _rolloff = value;
  return (*this);
}

Flt YSE::reverb::rolloff() {
  return _rolloff;
}

YSE::reverb& YSE::reverb::active(Bool value) {
  _active = value;
  return (*this);
}

Bool YSE::reverb::active() {
  return _active;
}

YSE::reverb& YSE::reverb::roomsize(Flt value) {
  Clamp(value, 0.f, 1.f);
  _roomsize = value;
  return (*this);
}

Flt YSE::reverb::roomsize() {
  return _roomsize;
}

YSE::reverb& YSE::reverb::damp(Flt value) {
  Clamp(value, 0.f, 1.f);
  _damp = value;
  return (*this);
}

Flt YSE::reverb::damp() {
  return _damp;
}

YSE::reverb& YSE::reverb::wet(Flt value) {
  Clamp(value, 0.f, 1.f);
  _wet = value;
  return (*this);
}

Flt YSE::reverb::wet() {
  return _wet;
}

YSE::reverb& YSE::reverb::dry(Flt value) {
  Clamp(value, 0.f, 1.f);
  _dry = value;
  return (*this);
}

Flt YSE::reverb::dry() {
  return _dry;
}

YSE::reverb& YSE::reverb::modFreq(Flt value) {
  if (value < 0) value = 0;
  _modFrequency = value;
  return (*this);
}

Flt YSE::reverb::modFreq() {
  return _modFrequency;
}

YSE::reverb& YSE::reverb::modWidth(Flt value) {
  if (value < 0) value = 0;
  _modWidth = value;
  return (*this);
}

Flt YSE::reverb::modWidth() {
  return _modWidth;
}

YSE::reverb& YSE::reverb::reflectionTime(Int reflection, Int value) {
  if (reflection > -1 && reflection < 4) {
    Clamp(value, 0, 2999);
    _earlyPtr[reflection] = value;
  }
  return (*this);
}

Int YSE::reverb::reflectionTime(Int reflection) {
  if (reflection > -1 && reflection < 4) return _earlyPtr[reflection];
  return -1;
}

YSE::reverb& YSE::reverb::reflectionGain(Int reflection, Flt value) {
  if (reflection > -1 && reflection < 4) {
    Clamp(value, 0.f, 1.f);
    _earlyGain[reflection] = value;
  }
  return (*this);
}

Flt YSE::reverb::reflectionGain(Int reflection) {
  if (reflection > -1 && reflection < 4) return _earlyGain[reflection];
  return -1;
}

YSE::reverb& YSE::reverb::preset(REVERB_PRESET value) {
  switch (value) {
  case REVERB_OFF:			roomsize(0.f).damp(0.f).wet(0.f).dry(1.f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_GENERIC:	roomsize(0.5f).damp(0.5f).wet(0.4f).dry(0.6f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_PADDED:		roomsize(0.1f).damp(0.9f).wet(0.1f).dry(0.9f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_ROOM:			roomsize(0.3f).damp(0.8f).wet(0.3f).dry(0.7f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_BATHROOM:	roomsize(0.2f).damp(0.1f).wet(0.7f).dry(0.3f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 20).reflectionTime(2, 50).reflectionTime(3, 85);
    reflectionGain(0, 1.f).reflectionGain(1, 0.7f).reflectionGain(2, 0.5f).reflectionGain(3, 0.3f);
    break;
  case REVERB_STONEROOM:roomsize(0.3f).damp(0.01f).wet(0.7f).dry(0.3f).modFreq(0).modWidth(0);
    reflectionTime(0, 30).reflectionTime(1, 70).reflectionTime(2, 100).reflectionTime(3, 150);
    reflectionGain(0, 0.8f).reflectionGain(1, 0.3f).reflectionGain(2, 0.5f).reflectionGain(3, 0.3f);
    break;
  case REVERB_LARGEROOM:roomsize(0.7f).damp(0.8f).wet(0.3f).dry(0.7f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_HALL:     roomsize(0.7f).damp(0.4f).wet(0.5f).dry(0.5f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  case REVERB_CAVE:     roomsize(1.0f).damp(0.3f).wet(0.7f).dry(0.3f).modFreq(0.f).modWidth(0.f);
    reflectionTime(0, 100).reflectionTime(1, 250).reflectionTime(2, 400).reflectionTime(3, 800);
    reflectionGain(0, 0.8f).reflectionGain(1, 0.6f).reflectionGain(2, 0.4f).reflectionGain(3, 0.5f);
    break;
  case REVERB_SEWERPIPE:roomsize(0.5f).damp(0.1f).wet(0.7f).dry(0.3f).modFreq(3.5f).modWidth(20.0f);
    reflectionTime(0, 200).reflectionTime(1, 600).reflectionTime(2, 1100).reflectionTime(3, 0);
    reflectionGain(0, 0.05f).reflectionGain(1, 0.04f).reflectionGain(2, 0.01f).reflectionGain(3, 0.f);
    break;
  case REVERB_UNDERWATER: roomsize(0.1f).damp(0.2f).wet(0.7f).dry(0.3f).modFreq(3.5f).modWidth(20.0f);
    reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2, 0).reflectionTime(3, 0);
    reflectionGain(0, 0.f).reflectionGain(1, 0.f).reflectionGain(2, 0.f).reflectionGain(3, 0.f);
    break;
  }

  return *this;
}
