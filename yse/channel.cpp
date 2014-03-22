/*
  ==============================================================================

    channel.cpp
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/

#include "channel.hpp"
#include "utils/misc.hpp"
#include "internal/global.h"
#include "implementations/channelImplementation.h"
#include "implementations/logImplementation.h"
#include "internal/deviceManager.h"
#include "internal/channelManager.h"
#include "reverb/reverbManager.h"

YSE::channel::channel() : pimpl(nullptr), allowVirtual(true), volume(1.f)
{}

YSE::channel& YSE::channel::create(const char * name, channel& parent) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_CHANNEL_OBJECT_IN_USE);
    /* if you get an assertion here, it means you're using the 
       create function on a channel that is already created.
    */
    jassertfalse
  }
  
  pimpl = INTERNAL::Global.getChannelManager().addImplementation(name, this);
  pimpl->parent = parent.pimpl;
  INTERNAL::Global.getChannelManager().setup(pimpl);
  return *this;
}

void YSE::channel::createGlobal() {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_CHANNEL_OBJECT_IN_USE);
    /* if you get an assertion here, it means you're using the
    create function on a channel that is already created.
    */
    jassertfalse
  }

  // the global channel will be created instantly because no audio
  // thread can be running before this is ready anyway 
  pimpl = INTERNAL::Global.getChannelManager().addImplementation("Master channel", this);  
  INTERNAL::Global.getChannelManager().setMaster(pimpl);
}

YSE::channel::~channel() {
  if (pimpl != nullptr) {
    pimpl->head = nullptr;
    pimpl = nullptr;
  }
}

YSE::channel& YSE::channel::setVolume(Flt value) {
  Clamp(value, 0.f, 1.f);
  channelMessage m;
  m.message = CM_VOLUME;
  m.floatValue = value;
  messages.push(m);
  volume = value; // only used for getVolume
  return (*this);
}

Flt YSE::channel::getVolume() {
  return volume;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  channelMessage m;
  m.message = CM_MOVE;
  m.ptrValue = &parent;
  messages.push(m);
  return (*this);
}

YSE::channel& YSE::channel::setVirtual(Bool value) {
  channelMessage m;
  m.message = CM_VIRTUAL;
  m.boolValue = true;
  messages.push(m);
  allowVirtual = value;
  return (*this);
}

bool YSE::channel::getVirtual() {
  return allowVirtual;
}

YSE::channel& YSE::channel::attachReverb() { 
  channelMessage m;
  m.message = CM_ATTACH_REVERB;
  m.boolValue = true;
  messages.push(m);
  return (*this);
}

YSE::channel & YSE::ChannelMaster() {
  return INTERNAL::Global.getChannelManager().master();
}

YSE::channel & YSE::ChannelFX() {
  return INTERNAL::Global.getChannelManager().FX();
}

YSE::channel & YSE::ChannelMusic() {
  return INTERNAL::Global.getChannelManager().music();
}

YSE::channel & YSE::ChannelAmbient() {
  return INTERNAL::Global.getChannelManager().ambient();
}

YSE::channel & YSE::ChannelVoice() {
  return INTERNAL::Global.getChannelManager().voice();
}

YSE::channel & YSE::ChannelGui() {
  return INTERNAL::Global.getChannelManager().gui();
}