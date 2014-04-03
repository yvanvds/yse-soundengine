/*
  ==============================================================================

    channel.cpp
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"

YSE::CHANNEL::interfaceObject::interfaceObject() : pimpl(nullptr), allowVirtual(true), volume(1.f)
{}

YSE::CHANNEL::interfaceObject::~interfaceObject() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

YSE::CHANNEL::interfaceObject & YSE::CHANNEL::interfaceObject::create(const char * name, channel& parent) {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->name = name;

  pimpl = CHANNEL::Manager().addImplementation(this);
  pimpl->parent = parent.pimpl;
  CHANNEL::Manager().setup(pimpl);
  return *this;
}

void YSE::channel::createGlobal() {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->name = "Master channel";

  // the global channel will be created instantly because no audio
  // thread can be running before this is ready anyway 
  pimpl = CHANNEL::Manager().addImplementation(this);  
  CHANNEL::Manager().setMaster(pimpl);
}


YSE::channel& YSE::channel::setVolume(Flt value) {
  Clamp(value, 0.f, 1.f);
  messageObject m;
  m.ID = VOLUME;
  m.floatValue = value;
  pimpl->sendMessage(m);
  volume = value; // only used for getVolume
  return (*this);
}

Flt YSE::channel::getVolume() {
  return volume;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  messageObject m;
  m.ID = MOVE;
  m.ptrValue = &parent;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::channel& YSE::channel::setVirtual(Bool value) {
  messageObject m;
  m.ID = VIRTUAL;
  m.boolValue = true;
  pimpl->sendMessage(m);
  allowVirtual = value;
  return (*this);
}

bool YSE::channel::getVirtual() {
  return allowVirtual;
}

bool YSE::channel::isValid() {
  return pimpl != nullptr;
}

YSE::channel& YSE::channel::attachReverb() { 
  messageObject m;
  m.ID = ATTACH_REVERB;
  m.boolValue = true;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::channel & YSE::ChannelMaster() {
  return CHANNEL::Manager().master();
}

YSE::channel & YSE::ChannelFX() {
  return CHANNEL::Manager().FX();
}

YSE::channel & YSE::ChannelMusic() {
  return CHANNEL::Manager().music();
}

YSE::channel & YSE::ChannelAmbient() {
  return CHANNEL::Manager().ambient();
}

YSE::channel & YSE::ChannelVoice() {
  return CHANNEL::Manager().voice();
}

YSE::channel & YSE::ChannelGui() {
  return CHANNEL::Manager().gui();
}