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

YSE::channel& YSE::channel::create(const char * name, channel& parent) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_CHANNEL_OBJECT_IN_USE);
    /* if you get an assertion here, it means you're using the 
       create function on a channel that is already created.
    */
    jassertfalse
    return *this;
  }
  
  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());

  pimpl = INTERNAL::Global.getChannelManager().addChannelImplementation(name);
  parent.pimpl->add(pimpl);

  // adjust buffers to parent (important for custom channels added after setting a device)
  pimpl->set(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
  for (UInt i = 0; i < pimpl->outConf.size(); i++) {
    pimpl->outConf[i].angle = parent.pimpl->outConf[i].angle;
  }
  pimpl->link = &pimpl;
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

  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());

  pimpl = INTERNAL::Global.getChannelManager().addChannelImplementation("mainMix");
  pimpl->link = &pimpl;
  INTERNAL::Global.getDeviceManager().setChannel(pimpl);
}

YSE::channel& YSE::channel::release() {
  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());
  if (pimpl) {
    INTERNAL::Global.getChannelManager().removeChannelImplementation(pimpl);
  }
  pimpl = NULL;
  return (*this);
}

YSE::channel::~channel() {
  release();
}

YSE::channel& YSE::channel::volume(Flt value) {
  pimpl->newVolume = value;
  return (*this);
}

Flt YSE::channel::volume() {
  return pimpl->newVolume;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());
  parent.pimpl->add(pimpl);
  return (*this);
}

YSE::channel& YSE::channel::set(Int count) {
  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());
  pimpl->set(count);
  return (*this);
}

YSE::channel& YSE::channel::pos(Int nr, Flt angle) {
  const ScopedLock lock(INTERNAL::Global.getDeviceManager().getLock());
  pimpl->pos(nr, angle);
  return (*this);
}

YSE::channel::channel() {
  pimpl = NULL;
}

Bool YSE::channel::valid() {
  if (pimpl) return true;
  return false;
}

YSE::channel& YSE::channel::attachReverb() { 
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