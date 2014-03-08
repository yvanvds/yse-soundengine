/*
  ==============================================================================

    sound.cpp
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#include "sound.hpp"
#include "internal/global.h"
#include "implementations/soundImplementation.h"
#include "implementations/channelImplementation.h"
#include "implementations/logImplementation.h"
#include "internal/soundManager.h"
#include "internal/channelManager.h"
#include "internal/deviceManager.h"
#include "utils/misc.hpp"

YSE::sound::sound() :
  pimpl(nullptr), dsp(nullptr),
  pos(0.f), spread(0), fadeAndStopTime(0), volume(0.f), speed(0.f),
  size(0.f), loop(0.f), time(0.f), 
  relative(false), doppler(true), pan2D(false), occlusion(false), streaming(false),
  length(0), intent(SI_NONE), status(SS_STOPPED) {}

YSE::sound& YSE::sound::create(const char * fileName, channel * ch, Bool loop, Flt volume, Bool streaming) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_IN_USE);
    return *this;
  }
  
  pimpl = INTERNAL::Global.getSoundManager().addImplementation(this);
  if (ch == nullptr) ch = &INTERNAL::Global.getChannelManager().master();

  if (pimpl->create(fileName, ch, loop, volume, streaming)) {
    INTERNAL::Global.getSoundManager().setup(pimpl);
  } else {
    pimpl->objectStatus = SIS_DELETE;
    pimpl = nullptr;
    INTERNAL::Global.getSoundManager().runDeleteJob();
#if defined YSE_DEBUG 
      /* if there's no implementation at this point, most likely
      loading a sound didn't work. Check working directory and
      ensure the sound is really there.
      */
      jassertfalse
#endif
  }
  return *this;
}

YSE::sound& YSE::sound::create(DSP::dspSourceObject & dsp, channel * ch, Flt volume) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_IN_USE);
    /* If you get an assertion here, it means that you're trying to use the create function on
       a sound object that's already in use.
    */
    jassertfalse
    return *this;
  }

  pimpl = INTERNAL::Global.getSoundManager().addImplementation(this);

  pimpl->addSourceDSP(dsp);
  if (ch == nullptr) pimpl->parent = INTERNAL::Global.getChannelManager().master().pimpl;
  else  pimpl->parent = ch->pimpl;
  pimpl->fader.set(volume);
  // we'll have to get created to true somehow when dsp objects are implemented

  return *this;
}


YSE::sound::~sound() {
  if (pimpl != nullptr) {
    pimpl->head = nullptr;
    pimpl = nullptr;
  }
}

Bool YSE::sound::isValid() {
  if (pimpl) return true;
  return false;
}

YSE::sound& YSE::sound::setPosition(const Vec &v) {
  if (pos != v) {
    pos = v;
    soundMessage m;
    m.message = SM_POS;
    m.vecValue[0] = v.x;
    m.vecValue[1] = v.y;
    m.vecValue[2] = v.z;
    messages.push(m);
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
    soundMessage m;
    m.message = SM_SPREAD;
    m.floatValue = value;
    messages.push(m);
  }
  return (*this);
}

Flt YSE::sound::getSpread() {
  return pimpl->spread;
}

YSE::sound& YSE::sound::setVolume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  if (volume!= value) {
    volume = value;
    soundMessage m;
    m.message = SM_VOLUME_VALUE;
    m.floatValue = value;
    messages.push(m);

    if (time > 0) {
      soundMessage m2;
      m2.message = SM_VOLUME_TIME;
      m2.uintValue = time;
      messages.push(m2);
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
    soundMessage m;
    m.message = SM_SPEED;
    m.floatValue = value;
    messages.push(m);
  }
  return (*this);
}

Flt YSE::sound::getSpeed() {
  return speed;
}

YSE::sound& YSE::sound::setSize(Flt value) {
  if (size != value) {
    size = value;
    soundMessage m;
    m.message = SM_SIZE;
    m.floatValue = value;
    messages.push(m);
  }
  return (*this);
}

Flt YSE::sound::getSize() {
  return size;
}

YSE::sound& YSE::sound::setLooping(Bool value) {
  if (loop != value) {
    loop = value;
    soundMessage m;
    m.message = SM_LOOP;
    m.boolValue = value;
    messages.push(m);
  }
  return (*this);
}

Bool YSE::sound::isLooping() {
  return loop;
}


YSE::sound& YSE::sound::play() {
  if (intent != SI_PLAY) {
    intent = SI_PLAY;
    soundMessage m;
    m.message = SM_INTENT;
    m.intentValue = SI_PLAY;
    messages.push(m);
  }
  return (*this);
}

YSE::sound& YSE::sound::pause() {
  if (intent != SI_PAUSE) {
    intent = SI_PAUSE;
    soundMessage m;
    m.message = SM_INTENT;
    m.intentValue = SI_PAUSE;
    messages.push(m);
  }
  return (*this);
}

YSE::sound& YSE::sound::stop() {
  if (intent != SI_STOP) {
    intent = SI_STOP;
    soundMessage m;
    m.message = SM_INTENT;
    m.intentValue = SI_STOP;
    messages.push(m);
  }
  return (*this);
}

YSE::sound& YSE::sound::toggle() {
  intent = SI_TOGGLE;
  soundMessage m;
  m.message = SM_INTENT;
  m.intentValue = SI_TOGGLE;
  messages.push(m);
  return (*this);
}

YSE::sound& YSE::sound::restart() {
  if (intent != SI_RESTART) {
    intent = SI_RESTART;
    soundMessage m;
    m.message = SM_INTENT;
    m.intentValue = SI_RESTART;
    messages.push(m);
  }
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
    soundMessage m;
    m.message = SM_OCCLUSION;
    m.boolValue = value;
    messages.push(m);
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
    soundMessage m;
    m.message = SM_DSP;
    m.ptrValue = value;
    messages.push(m);
  }
  return (*this);
}

YSE::DSP::dspObject * YSE::sound::getDSP() {
  return dsp;
}

YSE::sound& YSE::sound::setTime(Flt value) {
  // don't compare with local time var in this case because 
  // that value is set by the implementation
  soundMessage m;
  m.message = SM_TIME;
  m.floatValue = value;
  messages.push(m);
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
    soundMessage m;
    m.message = SM_RELATIVE;
    m.boolValue = value;
    messages.push(m);
  }
  return (*this);
}

Bool YSE::sound::isRelative() {
  return relative;
}

YSE::sound& YSE::sound::setDoppler(Bool value) {
  if (doppler != value) {
    doppler = value;
    soundMessage m;
    m.message = SM_DOPPLER;
    m.boolValue = value;
    messages.push(m);
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
    soundMessage m;
    m.message = SM_PAN2D;
    m.boolValue = value;
    messages.push(m);
  }
  return (*this);
}

Bool YSE::sound::is2D() {
  return pan2D;
}

Bool YSE::sound::isReady() {
  if (pimpl && pimpl->objectStatus == SIS_READY) return true;
  return false;
}

YSE::sound& YSE::sound::fadeAndStop(UInt time) {
  soundMessage m;
  m.message = SM_FADE_AND_STOP;
  m.uintValue = time;
  messages.push(m);
  return (*this);
}

YSE::sound& YSE::sound::moveTo(channel & target) {
  if (parent != &target) {
    parent = &target;
    soundMessage m;
    m.message = SM_MOVE;
    m.ptrValue = parent;
    messages.push(m);
  }
  return (*this);
}