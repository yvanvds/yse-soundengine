/*
  ==============================================================================

    sound.cpp
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "../synth/synthInterface.hpp"

YSE::SOUND::interfaceObject::interfaceObject() :
  pimpl(nullptr), dsp(nullptr),
  pos(0.f), spread(0), fadeAndStopTime(0), volume(0.f), speed(1.f),
  size(0.f), loop(0.f), time(0.f), 
  relative(false), doppler(true), pan2D(false), occlusion(false), streaming(false),
  length(0), status(SS_STOPPED) {}

YSE::SOUND::interfaceObject::~interfaceObject() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

YSE::sound& YSE::sound::create(const char * fileName, channel * ch, Bool loop, Flt volume, Bool streaming) {
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

YSE::sound& YSE::sound::create(YSE::DSP::buffer & buffer, channel * ch, Bool loop, Flt volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(MULTICHANNELBUFFER & buffer, channel * ch, Bool loop, Flt volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(DSP::dspSourceObject & dsp, channel * ch, Flt volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(dsp, ch, volume);
  SOUND::Manager().setup(pimpl);

  return *this;
}

YSE::sound& YSE::sound::create(SYNTH::interfaceObject & synth, channel *ch, Flt volume) {
  assert(pimpl == nullptr);
  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(synth.pimpl, ch, volume);
  SOUND::Manager().setup(pimpl);
  return *this;
}

#if defined PUBLIC_JUCE
/**
This is an extra create function which enables you to pass a Juce InputStream with audio data
to YSE. This is usefull if you use Juce to create an application. But since it creates an additional
depency on the Juce library, it has to be enabled by including the preprocessor definition
PUBLIC_JUCE in your project settings.
*/
YSE::sound& YSE::sound::create(juce::InputStream * source, channel * ch, Bool loop, Flt volume, Bool streaming) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  if (pimpl->create(source, ch, loop, volume, streaming)) {
    SOUND::Manager().setup(pimpl);
  }
  else {
    pimpl->setStatus(OBJECT_RELEASE);
    pimpl = nullptr;
  }
  return *this;
}
#endif

Bool YSE::sound::isValid() {
  return pimpl != nullptr;
}

YSE::sound& YSE::sound::setPosition(const Vec &v) {
  if (pos != v) {
    pos = v;
    messageObject m;
    m.ID = POSITION;
    m.vecValue[0] = v.x;
    m.vecValue[1] = v.y;
    m.vecValue[2] = v.z;
    pimpl->sendMessage(m);
  } 
  return (*this);
}

YSE::Vec YSE::sound::getPosition() {
  return pos;
}

YSE::sound& YSE::sound::setSpread(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (spread != value) {
    spread = value;
    messageObject m;
    m.ID = SPREAD;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::getSpread() {
  return spread;
}

YSE::sound& YSE::sound::setVolume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  if (volume!= value) {
    volume = value;
    messageObject m;
    m.ID = VOLUME_VALUE;
    m.floatValue = value;
    pimpl->sendMessage(m);

    if (time > 0) {
      messageObject m2;
      m2.ID = VOLUME_TIME;
      m2.uintValue = time;
      pimpl->sendMessage(m2);
    }    
  }
  return (*this);
}

Flt YSE::sound::getVolume() {
  return volume;
}

YSE::sound& YSE::sound::setSpeed(Flt value) {
  if (speed != value) {
    speed = value;
    messageObject m;
    m.ID = SPEED;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::getSpeed() {
  return speed;
}

YSE::sound& YSE::sound::setSize(Flt value) {
  if (size != value) {
    size = value;
    messageObject m;
    m.ID = SIZE;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Flt YSE::sound::getSize() {
  return size;
}

YSE::sound& YSE::sound::setLooping(Bool value) {
  if (loop != value) {
    loop = value;
    messageObject m;
    m.ID = LOOP;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::isLooping() {
  return loop;
}


YSE::sound& YSE::sound::play() {
  messageObject m;
  m.ID = INTENT;
  m.intentValue = SI_PLAY;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::pause() {
  messageObject m;
  m.ID = INTENT;
  m.intentValue = SI_PAUSE;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::stop() {
  messageObject m;
  m.ID = INTENT;
  m.intentValue = SI_STOP;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::toggle() {
  messageObject m;
  m.ID = INTENT;
  m.intentValue = SI_TOGGLE;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::restart() {
  messageObject m;
  m.ID = INTENT;
  m.intentValue = SI_RESTART;
  pimpl->sendMessage(m);
  return (*this);
}

Bool YSE::sound::isPlaying() {
  return (status == SS_PLAYING || status == SS_PLAYING_FULL_VOLUME);
}

Bool YSE::sound::isPaused() {
  return status == SS_PAUSED;
}

Bool YSE::sound::isStopped() {
  return status == SS_STOPPED;
}

YSE::sound& YSE::sound::setOcclusion(Bool value) {
  if (occlusion != value) {
    occlusion = value;
    messageObject m;
    m.ID = OCCLUSION;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::getOcclusion() {
  return occlusion;
}

Bool YSE::sound::isStreaming() {
  return streaming;
}

YSE::sound& YSE::sound::setDSP(YSE::DSP::dspObject * value) {
  if (dsp != value) {
    dsp = value;
    messageObject m;
    m.ID = DSP;
    m.ptrValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

YSE::DSP::dspObject * YSE::sound::getDSP() {
  return dsp;
}

YSE::sound& YSE::sound::setTime(Flt value) {
  // don't compare with local time var in this case because 
  // that value is set by the implementation
  messageObject m;
  m.ID = TIME;
  m.floatValue = value;
  pimpl->sendMessage(m);
  return (*this);
}

Flt YSE::sound::getTime() {
  return time;
}

UInt YSE::sound::getLength() {
  return length;
}

YSE::sound& YSE::sound::setRelative(Bool value) {
  if (relative != value) {
    relative = value;
    messageObject m;
    m.ID = RELATIVE;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::isRelative() {
  return relative;
}

YSE::sound& YSE::sound::setDoppler(Bool value) {
  if (doppler != value) {
    doppler = value;
    messageObject m;
    m.ID = DOPPLER;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::getDoppler() {
  return doppler;
}

YSE::sound& YSE::sound::set2D(Bool value) {
  if (pan2D != value) {
    relative = value;
    doppler = !value;
    pan2D = value;
    messageObject m;
    m.ID = PAN2D;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

Bool YSE::sound::is2D() {
  return pan2D;
}

Bool YSE::sound::isReady() {
  if (pimpl && pimpl->getStatus() == OBJECT_READY) return true;
  return false;
}

YSE::sound& YSE::sound::fadeAndStop(UInt time) {
  messageObject m;
  m.ID = FADE_AND_STOP;
  m.uintValue = time;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::sound& YSE::sound::moveTo(channel & target) {
  if (parent != &target) {
    parent = &target;
    messageObject m;
    m.ID = MOVE;
    m.ptrValue = parent;
    pimpl->sendMessage(m);
  }
  return (*this);
}