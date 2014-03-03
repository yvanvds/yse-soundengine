/*
  ==============================================================================

    channel.cpp
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/

#include "channel.hpp"
#include "internal/global.h"
#include "implementations/channelImplementation.h"
#include "implementations/logImplementation.h"
#include "internal/deviceManager.h"
#include "internal/channelManager.h"
#include "internal/reverbManager.h"

YSE::channel::channel() : pimpl(NULL)
  
{}

YSE::channel& YSE::channel::create(const char * name, channel& parent) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_CHANNEL_OBJECT_IN_USE);
    /* if you get an assertion here, it means you're using the 
       create function on a channel that is already created.
    */
    jassertfalse
  }
  
  pimpl = INTERNAL::Global.getChannelManager().add(name, this);
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
  pimpl = INTERNAL::Global.getChannelManager().add("Master channel", this);
  INTERNAL::Global.getChannelManager().setMaster(pimpl);
}

YSE::channel::~channel() {
  if (pimpl != NULL) {
    pimpl->head = NULL;
    pimpl = NULL;
  }
}

YSE::channel& YSE::channel::setVolume(Flt value) {
  volume = value;
  flagVolume = true;
  return (*this);
}

Flt YSE::channel::getVolume() {
  return volume;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  newChannel = &parent;
  moveChannel = true;
  return (*this);
}

YSE::channel& YSE::channel::attachReverb() { 
  //TODO: this is not threadSafe!
  if (pimpl) {
    INTERNAL::Global.getReverbManager().attachToChannel(pimpl);
  }
  return *this;
}

YSE::channel & YSE::ChannelMainMix() {
  return INTERNAL::Global.getChannelManager().mainMix();
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