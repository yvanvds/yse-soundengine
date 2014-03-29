/*
  ==============================================================================

    channel.cpp
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"

YSE::CHANNEL::interfaceObject::interfaceObject() : allowVirtual(true), volume(1.f)
{}

YSE::CHANNEL::interfaceObject & YSE::CHANNEL::interfaceObject::create(const char * name, channel& parent) {
  interfaceTemplate<channelSubSystem>::create();
  this->name = name;

  pimpl = INTERNAL::Global().getChannelManager().addImplementation(this);
  pimpl->parent = parent.pimpl;
  INTERNAL::Global().getChannelManager().setup(pimpl);
  return *this;
}

void YSE::channel::createGlobal() {
  interfaceTemplate<channelSubSystem>::create();
  this->name = "Master channel";

  // the global channel will be created instantly because no audio
  // thread can be running before this is ready anyway 
  pimpl = INTERNAL::Global().getChannelManager().addImplementation(this);  
  INTERNAL::Global().getChannelManager().setMaster(pimpl);
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

YSE::channel& YSE::channel::attachReverb() { 
  messageObject m;
  m.ID = ATTACH_REVERB;
  m.boolValue = true;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::channel & YSE::ChannelMaster() {
  return INTERNAL::Global().getChannelManager().master();
}

YSE::channel & YSE::ChannelFX() {
  return INTERNAL::Global().getChannelManager().FX();
}

YSE::channel & YSE::ChannelMusic() {
  return INTERNAL::Global().getChannelManager().music();
}

YSE::channel & YSE::ChannelAmbient() {
  return INTERNAL::Global().getChannelManager().ambient();
}

YSE::channel & YSE::ChannelVoice() {
  return INTERNAL::Global().getChannelManager().voice();
}

YSE::channel & YSE::ChannelGui() {
  return INTERNAL::Global().getChannelManager().gui();
}