/*
  ==============================================================================

    reverb.cpp
    Created: 1 Feb 2014 7:02:58pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"



YSE::reverb::reverb(bool global) 
  : pimpl(nullptr), connectedToManager(false), active(true), roomsize(0.5f), damp(0.5),
    wet(0.5f), dry(0.5f), modFrequency(0),
    modWidth(0), global(global) {}

YSE::reverb::~reverb() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

void YSE::reverb::create() {
  assert(pimpl == nullptr);

  for (Int i = 0; i < 4; i++) {
    earlyPtr[i] = 0;
    earlyGain[i] = 0;
  }

  pimpl = REVERB::Manager().addImplementation(this);
  REVERB::Manager().setup(pimpl);
}

Bool YSE::reverb::isValid() {
  return pimpl != nullptr;
}

YSE::reverb & YSE::reverb::setPosition(const Pos &value) {
  if (position != value) {
    position = value;
    REVERB::messageObject m;
    m.ID = REVERB::POSITION;
    m.vecValue[0] = value.x;
    m.vecValue[1] = value.y;
    m.vecValue[2] = value.z;
    pimpl->sendMessage(m);
  }
  return *this;
}

YSE::Pos YSE::reverb::getPosition() {
  return position;
}

YSE::reverb & YSE::reverb::setSize(Flt value) {
  if (value < 0) value = 0;
  if (size != value) {
    size = value;
    REVERB::messageObject m;
    m.ID = REVERB::SIZE;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getSize() {
  return size;
}

YSE::reverb & YSE::reverb::setRollOff(Flt value) {
  if (value < 0) value = 0;
  if (rolloff != value) {
    rolloff = value;
    REVERB::messageObject  m;
    m.ID = REVERB::ROLLOFF;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getRollOff() {
  return rolloff;
}

YSE::reverb & YSE::reverb::setActive(Bool value) {
  if (active != value) {
    active = value;
    REVERB::messageObject  m;
    m.ID = REVERB::ACTIVE;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::reverb::getActive() {
  return active;
}

YSE::reverb & YSE::reverb::setRoomSize(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (roomsize != value) {
    roomsize = value;
    REVERB::messageObject m;
    m.ID = REVERB::ROOMSIZE;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getRoomSize() {
  return roomsize;
}

YSE::reverb & YSE::reverb::setDamping(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (damp != value) {
    damp = value;
    REVERB::messageObject  m;
    m.ID = REVERB::DAMP;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getDamping() {
  return damp;
}

YSE::reverb & YSE::reverb::setDryWetBalance(Flt dry, Flt wet) {
  Clamp(dry, 0.f, 1.f);
  Clamp(wet, 0.f, 1.f);
#if defined debug
  if ((dry + wet) > 1.f) {
    /*If you get an assertion here, it means that dry and
      wet values combined add up to more than 1. This will
      result in distored sound.*/
    assert(false);
  }
#endif
  if (this->dry != dry || this->wet != wet) {
    this->wet = wet;
    this->dry = dry;
    REVERB::messageObject m;
    m.ID = REVERB::DRY_WET;
    // abusing vec here a bit
    m.vecValue[0] = dry;
    m.vecValue[1] = wet;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getWet() {
  return wet;
}

Flt YSE::reverb::getDry() {
  return dry;
}

YSE::reverb & YSE::reverb::setModulation(Flt frequency, Flt width) {
  if (frequency < 0) frequency = 0;
  if (width < 0) width = 0;
  if (modFrequency != frequency || modWidth != width) {
    modFrequency = frequency;
    modWidth = width;
    REVERB::messageObject m;
    m.ID = REVERB::MODULATION;
    m.vecValue[0] = frequency;
    m.vecValue[1] = width;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::reverb::getModulationFrequency() {
  return modFrequency;
}

Flt YSE::reverb::getModulationWidth() {
  return modWidth;
}

YSE::reverb & YSE::reverb::setReflection(Int reflection, Int time, Flt gain) {
  if (reflection >= 0 && reflection < 4) {
    Clamp(time, 0, 2999);
    Clamp(gain, 0.f, 1.f);
    if (earlyPtr[reflection] != time || earlyGain[reflection] != gain) {
      earlyPtr[reflection] = time;
      earlyGain[reflection] = gain;
      REVERB::messageObject m;
      m.ID = REVERB::REFLECTION;
      // TODO: can a union contain an int + 2 floats as content???
      m.vecValue[0] = (Flt)reflection;
      m.vecValue[1] = (Flt)time;
      m.vecValue[2] = gain;
      pimpl->sendMessage(m);
    }
  }
  else {
    assert(false);
    // reflection value must be from 0 to 4
  }
  return (*this);
}

Int YSE::reverb::getReflectionTime(Int reflection) {
  if (reflection >= 0 && reflection < 4) return earlyPtr[reflection];
  else {
    assert(false);
    // reflection value must be from 0 to 4
  }
  return -1;
}

Flt YSE::reverb::getReflectionGain(Int reflection) {
  if (reflection >= 0 && reflection < 4) return earlyGain[reflection];
  else {
    assert(false);
    // reflection value must be from 0 to 4
  }
  return -1;
}

YSE::reverb & YSE::reverb::setPreset(REVERB_PRESET value) {
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
