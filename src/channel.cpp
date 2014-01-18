#include "stdafx.h"
#include "channel.hpp"
#include "internal/channelimpl.h"
#include "internal/internalObjects.h"
#include "utils/error.hpp"
#include "backend/reverbbackend.h"

YSE::channel YSE::ChannelAmbient;
YSE::channel YSE::ChannelFX;
YSE::channel YSE::ChannelGlobal;
YSE::channel YSE::ChannelGui;
YSE::channel YSE::ChannelMusic;
YSE::channel YSE::ChannelVoice;


YSE::channel& YSE::channel::create() {
  create(ChannelGlobal);
  return *this;
}

YSE::channel& YSE::channel::create(channel& parent) {
  if (pimpl) {
    Error.emit(E_CHANNEL_OBJECT_IN_USE);
    return *this;
  }

  lock l(MTX);
  Channels().push_back(new channelimpl);
  pimpl = &Channels().back();

  parent.pimpl->add(pimpl);

  // adjust buffers to parent (important for custom channels added after setting a device)
  pimpl->out.resize(parent.pimpl->out.size());
  pimpl->outConf.resize(parent.pimpl->outConf.size());
  for (UInt i = 0; i < pimpl->outConf.size(); i++) {
    pimpl->outConf[i].angle = parent.pimpl->outConf[i].angle;
  }
  pimpl->link = &pimpl;
  return *this;
}

void YSE::channel::createGlobal() {
  if (pimpl) {
    Error.emit(E_CHANNEL_OBJECT_IN_USE);
  }
  lock l(MTX);

  Channels().push_back(new channelimpl);
  pimpl = &Channels().back();
  pimpl->link = &pimpl;


}

YSE::channel& YSE::channel::release() {
  if (pimpl) {
    pimpl->_release = true;
    pimpl->link = NULL;
  }
  pimpl = NULL;
  return (*this);
}

YSE::channel::~channel() {
  release();
}

YSE::channel& YSE::channel::volume(Flt value) {
  pimpl->_newVolume = value;
  pimpl->_setVolume = true;
  return (*this);
}

Flt YSE::channel::volume() {
  return pimpl->_currentVolume;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  lock l(MTX);
  parent.pimpl->add(pimpl);
  return (*this);
}

YSE::channel& YSE::channel::set(Int count) {
  lock l(MTX);
  pimpl->set(count);
  return (*this);
}

YSE::channel& YSE::channel::pos(Int nr, Flt angle) {
  lock l(MTX);
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

YSE::channel& YSE::channel::attachReverb(Bool value) {
  lock l(MTX);
  if (pimpl) {
    if (!value) {
      pimpl->reverb = NULL;
    } else {
      boost::ptr_list<channelimpl>::iterator i = Channels().begin();
      while (i != Channels().end()) {
        i->reverb = NULL;
        i++;
      }
      pimpl->reverb = &ReverbBackend;
    }
  }
  return *this;
}

YSE::channel& YSE::channel::underWater(Flt depth) {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
  } else {
    pimpl->underWaterDepth = depth;
  }
  return *this;
}

Flt YSE::channel::underWater() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return 0;
  }
  return pimpl->underWaterDepth;
}