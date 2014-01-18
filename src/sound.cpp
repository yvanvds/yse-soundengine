#include "stdafx.h"
#include "sound.hpp"
#include "internal/channelimpl.h"
#include "internal/internalObjects.h"
#include "utils/error.hpp"

YSE::sound::sound() {
  pimpl = NULL;
}

YSE::sound& YSE::sound::create(const char * fileName, const channel * const ch, Bool loop, Flt volume, Bool streaming) {
  if (pimpl) {
    Error.emit(E_SOUND_OBJECT_IN_USE);
    return *this;
  }
  lock l(MTX);

  Sounds().push_back(new soundimpl);
  pimpl = &Sounds().back();

  if (pimpl->create(fileName, streaming)) {
    pimpl->initialize();
    if (ch == NULL) ChannelGlobal.pimpl->add(pimpl);
    else ch->pimpl->add(pimpl);
    pimpl->looping = loop;
    pimpl->_fader.set(volume);
    pimpl->file->claim(); // increase usage counter
    pimpl->link = &pimpl;
  } else {
    Sounds().pop_back();
    pimpl = NULL;
  }
  return *this;
}

YSE::sound& YSE::sound::create(DSP::dspSource & dsp, const channel * const ch, Flt volume) {
  if (pimpl) {
    Error.emit(E_SOUND_OBJECT_IN_USE);
    return *this;
  }

  lock l(MTX);

  Sounds().push_back(new soundimpl);
  pimpl = &Sounds().back();

  pimpl->addSourceDSP(dsp);
  pimpl->initialize();
  if (ch == NULL) ChannelGlobal.pimpl->add(pimpl);
  else ch->pimpl->add(pimpl);
  pimpl->_fader.set(volume);
  pimpl->link = &pimpl;

  return *this;
}

YSE::sound& YSE::sound::release() {
  if (pimpl) {
    lock l(MTX);
    pimpl->_release = true;
    pimpl->_signalStop = true;
    pimpl->link = NULL;
  }
  return *this;
}

YSE::sound::~sound() {
  release();
}

Bool YSE::sound::valid() {
  if (pimpl) return true;
  return false;
}

YSE::sound& YSE::sound::pos(const Vec &v) {
  if (pimpl) pimpl->_pos = v;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::Vec YSE::sound::pos() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return Vec(0);
  }
  return pimpl->_pos;
}

YSE::sound& YSE::sound::spread3D(Flt value) {
  Clamp(value, 0, 1);
	if (pimpl) pimpl->_spread = value;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
	return (*this);
}

Flt YSE::sound::spread3D() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return 0;
  }
  return pimpl->_spread;
}

YSE::sound& YSE::sound::volume(Flt value, UInt time) {
	Clamp(value, 0, 1);
	if (pimpl) {
    pimpl->_setVolume = true;
    pimpl->_volumeValue = value;
    pimpl->_volumeTime = time;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
	return (*this);
}

Flt YSE::sound::volume() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return 0;
  }
  return pimpl->_currentVolume;
}

YSE::sound& YSE::sound::speed(Flt value) {
  if (pimpl) {
    if (pimpl->_streaming && value < 0) value = 0;
  	pimpl->_pitch = value;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Flt YSE::sound::speed() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return 0;
  }
  return pimpl->_pitch;
}

YSE::sound& YSE::sound::size(Flt value) {
  if (pimpl) {
  	if (value < 0) pimpl->_size = 0;
  	else pimpl->_size = value;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Flt YSE::sound::size() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return 0;
  }
  return pimpl->_size;
}

YSE::sound& YSE::sound::loop(Bool value) {
  if (pimpl) {
    pimpl->looping = value;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::loop() {
  if (pimpl == NULL) {
    Error.emit(E_SOUND_OBJECT_NO_INIT);
    return false;
  }
  return pimpl->looping;
}


YSE::sound& YSE::sound::play(UInt latency) {
  if (pimpl) {
    pimpl->_signalPlay = true;
    pimpl->_latency = latency;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::sound& YSE::sound::pause(UInt latency) {
  if (pimpl) {
    pimpl->_signalPause = true;
    pimpl->_latency = latency;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::sound& YSE::sound::stop(UInt latency) {
  if (pimpl) {
    pimpl->_signalStop = true;
    pimpl->_latency = latency;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::sound& YSE::sound::toggle(UInt latency) {
  if (pimpl) {
    pimpl->_signalToggle = true;
    pimpl->_latency = latency;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::sound& YSE::sound::restart(UInt latency) {
  if (pimpl) {
    pimpl->_signalRestart = true;
    pimpl->_latency = latency;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::playing() {
  if (pimpl) return (pimpl->_status == SS_PLAYING);
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

Bool YSE::sound::paused() {
  if (pimpl)  return (pimpl->_status == SS_PAUSED);
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return true;
}

Bool YSE::sound::stopped() {
  if (pimpl) return (pimpl->_status == SS_STOPPED);
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return true;
}

YSE::sound& YSE::sound::occlusion(Bool value) {
  if (pimpl) pimpl->_occlusionActive = value;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::occlusion() {
  if (pimpl) return pimpl->_occlusionActive;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

Bool YSE::sound::streamed() {
  if (pimpl) return pimpl->_streaming;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

YSE::sound& YSE::sound::dsp(YSE::DSP::dsp & value) {
  if (pimpl) {
    pimpl->_postDspPtr = &value;
    pimpl->_setPostDSP = true;
  }
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

YSE::DSP::dsp * YSE::sound::dsp() {
  if (pimpl) return pimpl->_postDspPtr;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return NULL;
}

YSE::sound& YSE::sound::time(Flt value) {
  if (pimpl) {
    pimpl->newFilePos = value;
    pimpl->setFilePos = true;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Flt YSE::sound::time() {
  if (pimpl) return pimpl->currentFilePos;
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return 0;
}

UInt YSE::sound::length() {
  if (pimpl) return pimpl->_length;
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return 0;
}

YSE::sound& YSE::sound::relative(Bool value) {
  if (pimpl) pimpl->_relative = value;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::relative() {
  if (pimpl) return pimpl->_relative;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

YSE::sound& YSE::sound::doppler(Bool value) {
  if (pimpl) pimpl->_noDoppler = !value;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::doppler() {
  if (pimpl) return !pimpl->_noDoppler;
  else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

YSE::sound& YSE::sound::pan2D(Bool value) {
  if (pimpl) {
    pimpl->_relative = value;
    pimpl->_noDoppler = value;
    pimpl->_pan2D = value;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}

Bool YSE::sound::pan2D() {
  if (pimpl) return pimpl->_pan2D;
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

Bool YSE::sound::ready() {
  if (pimpl) return !pimpl->_loading;
  Error.emit(E_SOUND_OBJECT_NO_INIT);
  return false;
}

YSE::sound& YSE::sound::fadeAndStop(UInt time) {
	if (pimpl) {
    pimpl->_setFadeAndStop = true;
    pimpl->_fadeAndStopTime = time;
  } else Error.emit(E_SOUND_OBJECT_NO_INIT);
  return (*this);
}
