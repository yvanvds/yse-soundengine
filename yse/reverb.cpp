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

YSE::reverb& YSE::reverb::setPosition(const Vec &value) {
  _position = value;
  return *this;
}

YSE::Vec YSE::reverb::getPosition() {
  return _position;
}

YSE::reverb& YSE::reverb::setSize(Flt value) {
  if (value < 0) value = 0;
  _size = value;
  return (*this);
}

Flt YSE::reverb::getSize() {
  return _size;
}

YSE::reverb& YSE::reverb::setRollOff(Flt value) {
  if (value < 0) value = 0;
  _rolloff = value;
  return (*this);
}

Flt YSE::reverb::getRollOff() {
  return _rolloff;
}

YSE::reverb& YSE::reverb::setActive(Bool value) {
  _active = value;
  return (*this);
}

Bool YSE::reverb::getActive() {
  return _active;
}

YSE::reverb& YSE::reverb::setRoomSize(Flt value) {
  Clamp(value, 0.f, 1.f);
  _roomsize = value;
  return (*this);
}

Flt YSE::reverb::getRoomSize() {
  return _roomsize;
}

YSE::reverb& YSE::reverb::setDamping(Flt value) {
  Clamp(value, 0.f, 1.f);
  _damp = value;
  return (*this);
}

Flt YSE::reverb::getDamping() {
  return _damp;
}

YSE::reverb& YSE::reverb::setDryWetBalance(Flt dry, Flt wet) {
  Clamp(dry, 0.f, 1.f);
  Clamp(wet, 0.f, 1.f);
#if defined debug
  if ((dry + wet) > 1.f) {
    /*If you get an assertion here, it means that dry and
      wet values combined add up to more than 1. This will
      result in distored sound.*/
    jassertfalse
  }
#endif
  _wet = wet;
  _dry = dry;
  return (*this);
}

Flt YSE::reverb::getWet() {
  return _wet;
}

Flt YSE::reverb::getDry() {
  return _dry;
}

YSE::reverb& YSE::reverb::setModulation(Flt frequency, Flt width) {
  if (frequency < 0) frequency = 0;
  if (width < 0) width = 0;
  _modFrequency = frequency;
  _modWidth = width;
  return (*this);
}

Flt YSE::reverb::getModulationFrequency() {
  return _modFrequency;
}

Flt YSE::reverb::getModulationWidth() {
  return _modWidth;
}

YSE::reverb& YSE::reverb::setReflection(Int reflection, Int time, Flt gain) {
  if (reflection > -1 && reflection < 4) {
    Clamp(time, 0, 2999);
    _earlyPtr[reflection] = time;
    Clamp(gain, 0.f, 1.f);
    _earlyGain[reflection] = gain;
  }
  return (*this);
}

Int YSE::reverb::getReflectionTime(Int reflection) {
  if (reflection > -1 && reflection < 4) return _earlyPtr[reflection];
  return -1;
}

Flt YSE::reverb::getReflectionGain(Int reflection) {
  if (reflection > -1 && reflection < 4) return _earlyGain[reflection];
  return -1;
}

YSE::reverb& YSE::reverb::preset(REVERB_PRESET value) {
  switch (value) {
  case REVERB_OFF:			setRoomSize(0.f).setDamping(0.f).setDryWetBalance(1.f, 0.f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_GENERIC:	setRoomSize(0.5f).setDamping(0.5f).setDryWetBalance(0.6f, 0.4f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_PADDED:		setRoomSize(0.1f).setDamping(0.9f).setDryWetBalance(0.9f, 0.1f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_ROOM:			setRoomSize(0.3f).setDamping(0.8f).setDryWetBalance(0.7f, 0.3f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_BATHROOM:	setRoomSize(0.2f).setDamping(0.1f).setDryWetBalance(0.3f, 0.7f).setModulation(0.f, 0.f);
    setReflection(0, 0, 1.f).setReflection(1, 20, 0.7f).setReflection(2, 50, 0.5f).setReflection(3, 85, 0.3f);
    break;
  case REVERB_STONEROOM:setRoomSize(0.3f).setDamping(0.01f).setDryWetBalance(0.3f, 0.7f).setModulation(0.f, 0.f);
    setReflection(0, 30, 0.8f).setReflection(1, 70, 0.3f).setReflection(2, 100, 0.5f).setReflection(3, 150, 0.3f);
    break;
  case REVERB_LARGEROOM:setRoomSize(0.7f).setDamping(0.8f).setDryWetBalance(0.7f, 0.3f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_HALL:     setRoomSize(0.7f).setDamping(0.4f).setDryWetBalance(0.5f, 0.5f).setModulation(0.f, 0.f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  case REVERB_CAVE:     setRoomSize(1.0f).setDamping(0.3f).setDryWetBalance(0.3f, 0.7f).setModulation(0.f, 0.f);
    setReflection(0, 100, 0.8f).setReflection(1, 250, 0.6f).setReflection(2, 400, 0.4f).setReflection(3, 800, 0.5f);
    break;
  case REVERB_SEWERPIPE:setRoomSize(0.5f).setDamping(0.1f).setDryWetBalance(0.3f, 0.7f).setModulation(3.5f, 20.0f);
    setReflection(0, 200, 0.05f).setReflection(1, 600, 0.04f).setReflection(2, 1100, 0.01f).setReflection(3, 0, 0.f);
    break;
  case REVERB_UNDERWATER: setRoomSize(0.1f).setDamping(0.2f).setDryWetBalance(0.3f, 0.7f).setModulation(3.5f, 20.0f);
    setReflection(0, 0, 0.f).setReflection(1, 0, 0.f).setReflection(2, 0, 0.f).setReflection(3, 0, 0.f);
    break;
  }

  return *this;
}
