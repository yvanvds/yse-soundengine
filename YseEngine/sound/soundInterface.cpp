/*
  ==============================================================================

    sound.cpp
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "patcher/patcherImplementation.h"
//#include "../synth/synthInterface.hpp"

YSE::sound::sound() :
  pimpl(nullptr), _dsp(nullptr),
  _pos(0.f), _spread(0), _fadeAndStopTime(0), _volume(0.f), _speed(1.f),
  _size(0.f), _loop(0.f), 
  _relative(false), _doppler(true), _pan2D(false), _occlusion(false) {}

YSE::sound::~sound() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
	Log().sendMessage("removed sound");
    pimpl = nullptr;
  }
}

YSE::sound& YSE::sound::create(const char * fileName, channel * ch, bool loop, float volume, bool streaming) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  if (pimpl->create(fileName, ch, loop, volume, streaming)) {
    SOUND::Manager().setup(pimpl);
  } else {
    pimpl->setStatus(OBJECT_RELEASE);
    pimpl = nullptr;
  }
  return *this;
}

YSE::sound& YSE::sound::create(YSE::DSP::buffer & buffer, channel * ch, bool loop, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(MULTICHANNELBUFFER & buffer, channel * ch, bool loop, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(YSE::DSP::dspSourceObject & dsp, channel * ch, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(dsp, ch, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(YSE::patcher & patch, channel * ch, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(patch.pimpl, ch, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

/*YSE::sound& YSE::sound::create(SYNTH::interfaceObject & synth, channel *ch, Flt volume) {
  assert(pimpl == nullptr);
  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(synth.pimpl, ch, volume);
  SOUND::Manager().setup(pimpl);
  return *this;
}*/


Bool YSE::sound::isValid() {
  return pimpl != nullptr;
}

YSE::sound& YSE::sound::pos(const Pos &v) {
  if (_pos != v) {
    _pos = v;
    SOUND::messageObject m;
    m.ID = SOUND::POSITION;
    m.vecValue[0] = v.x;
    m.vecValue[1] = v.y;
    m.vecValue[2] = v.z;
    pimpl->sendMessage(m);
  } 
  return (*this);
}

YSE::Pos YSE::sound::pos() {
  return _pos;
}

YSE::sound& YSE::sound::spread(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (_spread != value) {
    _spread = value;
    SOUND::messageObject m;
    m.ID = SOUND::SPREAD;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::spread() {
  return _spread;
}

YSE::sound& YSE::sound::volume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  if (_volume!= value) {
    _volume = value;
    SOUND::messageObject m;
    m.ID = SOUND::VOLUME_VALUE;
    m.floatValue = value;
    pimpl->sendMessage(m);

    if (time > 0) {
      SOUND::messageObject m2;
      m2.ID = SOUND::VOLUME_TIME;
      m2.uintValue = time;
      pimpl->sendMessage(m2);
    }    
  }
  return (*this);
}

Flt YSE::sound::volume() {
  return _volume;
}

YSE::sound& YSE::sound::speed(Flt value) {
  if (_speed != value) {
    _speed = value;
    SOUND::messageObject m;
    m.ID = SOUND::SPEED;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::speed() {
  return _speed;
}

YSE::sound& YSE::sound::size(Flt value) {
  if (_size != value) {
    _size = value;
    SOUND::messageObject m;
    m.ID = SOUND::SIZE;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::size() {
  return _size;
}

YSE::sound& YSE::sound::looping(Bool value) {
  if (_loop != value) {
    _loop = value;
    SOUND::messageObject m;
    m.ID = SOUND::LOOP;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::looping() {
  return _loop;
}


YSE::sound& YSE::sound::play() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_PLAY;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::pause() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_PAUSE;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::stop() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_STOP;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::toggle() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_TOGGLE;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::restart() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_RESTART;
  pimpl->sendMessage(m);
  return (*this);
}

Bool YSE::sound::isPlaying() {
  return (pimpl->_head_status == SS_PLAYING || pimpl->_head_status == SS_PLAYING_FULL_VOLUME);
}

Bool YSE::sound::isPaused() {
  return pimpl->_head_status == SS_PAUSED;
}

Bool YSE::sound::isStopped() {
  return pimpl->_head_status == SS_STOPPED;
}

YSE::sound& YSE::sound::occlusion(Bool value) {
  if (_occlusion != value) {
    _occlusion = value;
    SOUND::messageObject m;
    m.ID = SOUND::OCCLUSION;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::occlusion() {
  return _occlusion;
}

Bool YSE::sound::isStreaming() {
  return pimpl->_head_streaming;
}

YSE::sound& YSE::sound::setDSP(YSE::DSP::dspObject * value) {
  if (_dsp != value) {
    _dsp = value;
    SOUND::messageObject m;
    m.ID = SOUND::DSP;
    m.ptrValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

YSE::DSP::dspObject * YSE::sound::getDSP() {
  return _dsp;
}

YSE::sound& YSE::sound::time(Flt value) {
  // don't compare with local time var in this case because 
  // that value is set by the implementation
  SOUND::messageObject m;
  m.ID = SOUND::TIME;
  m.floatValue = value;
  pimpl->sendMessage(m);
  return (*this);
}

Flt YSE::sound::time() {
  return pimpl->_head_time;
}

UInt YSE::sound::length() {
  return pimpl->_head_length;
}

YSE::sound& YSE::sound::relative(Bool value) {
  if (_relative != value) {
    _relative = value;
    SOUND::messageObject m;
    m.ID = SOUND::RELATIVE;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::relative() {
  return _relative;
}

YSE::sound& YSE::sound::doppler(Bool value) {
  if (_doppler != value) {
    _doppler = value;
    SOUND::messageObject m;
    m.ID = SOUND::DOPPLER;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::doppler() {
  return _doppler;
}

YSE::sound& YSE::sound::pan2D(Bool value) {
  if (_pan2D != value) {
    _relative = value;
    _doppler = !value;
    _pan2D = value;
    SOUND::messageObject m;
    m.ID = SOUND::PAN2D;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::pan2D() {
  return _pan2D;
}

Bool YSE::sound::isReady() {
  if (pimpl && pimpl->getStatus() == OBJECT_READY) return true;
  return false;
}

YSE::sound& YSE::sound::fadeAndStop(UInt time) {
  SOUND::messageObject m;
  m.ID = SOUND::FADE_AND_STOP;
  m.uintValue = time;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::moveTo(channel & target) {
  if (_parent != &target) {
    _parent = &target;
    SOUND::messageObject m;
    m.ID = SOUND::MOVE;
    m.ptrValue = _parent;
    pimpl->sendMessage(m);
  }
  return (*this);
}