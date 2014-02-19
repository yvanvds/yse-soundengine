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

YSE::sound::sound() {
  pimpl = NULL;
}

YSE::sound& YSE::sound::create(const char * fileName, const channel * const ch, Bool loop, Flt volume, Bool streaming) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_IN_USE);
    return *this;
  }
  
  pimpl = INTERNAL::Global.getSoundManager().addImplementation();

  if (pimpl->create(fileName, streaming)) {
    pimpl->initialize();
    if (ch == NULL) INTERNAL::Global.getChannelManager().mainMix().pimpl->add(pimpl);
    else ch->pimpl->add(pimpl);
    pimpl->looping_dsp = loop;
    pimpl->fader_dsp.set(volume);
    pimpl->link = &pimpl;
  }
  else {
    INTERNAL::Global.getSoundManager().removeImplementation(pimpl);
    pimpl = NULL;
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

YSE::sound& YSE::sound::create(DSP::dspSourceObject & dsp, const channel * const ch, Flt volume) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_IN_USE);
    /* If you get an assertion here, it means that you're trying to use the create function on
       a sound object that's already in use.
    */
    jassertfalse
    return *this;
  }

  pimpl = INTERNAL::Global.getSoundManager().addImplementation();

  pimpl->addSourceDSP(dsp);
  pimpl->initialize();
  if (ch == NULL) INTERNAL::Global.getChannelManager().mainMix().pimpl->add(pimpl);
  else ch->pimpl->add(pimpl);
  pimpl->fader_dsp.set(volume);
  pimpl->link = &pimpl;

  return *this;
}

YSE::sound& YSE::sound::release() {
  if (pimpl) {
    pimpl->_release = true;
    pimpl->signalStop_dsp = true;
    pimpl->link = NULL;
  }
  return *this;
}

YSE::sound::~sound() {
  release();
}

Bool YSE::sound::isValid() {
  if (pimpl) return true;
  return false;
}

YSE::sound& YSE::sound::setPosition(const Vec &v) {
  if (pimpl) {
    pimpl->_pos = v;
  } else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
       a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::Vec YSE::sound::getPosition() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return Vec(0);
  }
  return pimpl->_pos;
}

YSE::sound& YSE::sound::setSpread(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (pimpl) pimpl->spread_dsp = value;
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Flt YSE::sound::getSpread() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return 0;
  }
  return pimpl->spread_dsp;
}

YSE::sound& YSE::sound::setVolume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  if (pimpl) {
    pimpl->setVolume_dsp = true;
    pimpl->volumeValue_dsp = value;
    pimpl->volumeTime_dsp = static_cast<Flt>(time);
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Flt YSE::sound::getVolume() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return 0;
  }
  return pimpl->currentVolume_dsp;
}

YSE::sound& YSE::sound::setSpeed(Flt value) {
  if (pimpl) {
    if (pimpl->streaming_dsp && value < 0) value = 0;
    pimpl->pitch_dsp = value;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Flt YSE::sound::getSpeed() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return 0;
  }
  return pimpl->pitch_dsp;
}

YSE::sound& YSE::sound::setSize(Flt value) {
  if (pimpl) {
    if (value < 0) pimpl->size_dsp = 0;
    else pimpl->size_dsp = value;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Flt YSE::sound::getSize() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return 0;
  }
  return pimpl->size_dsp;
}

YSE::sound& YSE::sound::setLooping(Bool value) {
  if (pimpl) {
    pimpl->looping_dsp = value;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Bool YSE::sound::isLooping() {
  if (pimpl == NULL) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
    return false;
  }
  return pimpl->looping_dsp;
}


YSE::sound& YSE::sound::play() {
  if (pimpl) {
    pimpl->signalPlay_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::sound& YSE::sound::pause() {
  if (pimpl) {
    pimpl->signalPause_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::sound& YSE::sound::stop() {
  if (pimpl) {
    pimpl->signalStop_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::sound& YSE::sound::toggle() {
  if (pimpl) {
    pimpl->signalToggle_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::sound& YSE::sound::restart() {
  if (pimpl) {
    pimpl->signalRestart_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Bool YSE::sound::isPlaying() {
  if (pimpl) return (pimpl->intent_dsp == SS_PLAYING);
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

Bool YSE::sound::isPaused() {
  if (pimpl)  return (pimpl->intent_dsp == SS_PAUSED);
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return true;
}

Bool YSE::sound::isStopped() {
  if (pimpl) return (pimpl->intent_dsp == SS_STOPPED);
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return true;
}

YSE::sound& YSE::sound::occlusion(Bool value) {
  if (pimpl) pimpl->_occlusionActive = value;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return (*this);
}

Bool YSE::sound::occlusion() {
  if (pimpl) return pimpl->_occlusionActive;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

Bool YSE::sound::isStreaming() {
  if (pimpl) return pimpl->streaming_dsp;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

YSE::sound& YSE::sound::attachDSP(YSE::DSP::dspObject & value) {
  if (pimpl) {
    pimpl->_postDspPtr = &value;
    pimpl->_setPostDSP = true;
  }
  else{
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

YSE::DSP::dspObject * YSE::sound::dsp() {
  if (pimpl) return pimpl->_postDspPtr;
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return NULL;
}

YSE::sound& YSE::sound::setTime(Flt value) {
  if (pimpl) {
    pimpl->newFilePos_dsp = value;
    pimpl->setFilePos_dsp = true;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Flt YSE::sound::getTime() {
  if (pimpl) return pimpl->currentFilePos_dsp;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return 0;
}

UInt YSE::sound::getLength() {
  if (pimpl) return pimpl->_length;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return 0;
}

YSE::sound& YSE::sound::setRelative(Bool value) {
  if (pimpl) pimpl->_relative = value;
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Bool YSE::sound::isRelative() {
  if (pimpl) return pimpl->_relative;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

YSE::sound& YSE::sound::setDoppler(Bool value) {
  if (pimpl) pimpl->_noDoppler = !value;
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Bool YSE::sound::getDoppler() {
  if (pimpl) return !pimpl->_noDoppler;
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return false;
}

YSE::sound& YSE::sound::set2D(Bool value) {
  if (pimpl) {
    pimpl->_relative = value;
    pimpl->_noDoppler = value;
    pimpl->_pan2D = value;
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}

Bool YSE::sound::is2D() {
  if (pimpl) return pimpl->_pan2D;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

Bool YSE::sound::isReady() {
  if (pimpl) return !pimpl->loading_dsp;
  
  INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
  /* if this happens, you're trying to access
  a sound before using it's create function.
  */
  jassertfalse
  return false;
}

YSE::sound& YSE::sound::fadeAndStop(UInt time) {
  if (pimpl) {
    pimpl->setFadeAndStop_dsp = true;
    pimpl->fadeAndStopTime_dsp = static_cast<Flt>(time);
  }
  else {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_NO_INIT);
    /* if this happens, you're trying to access
    a sound before using it's create function.
    */
    jassertfalse
  }
  return (*this);
}
