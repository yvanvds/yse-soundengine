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
  pimpl(NULL),
  flagPos   (false), posValue       (0),
  flagSpread(false), spread         (0),
  flagFade  (false), fadeAndStopTime(0),
  flagVolume(false), volumeValue    (0.f), volumeTime(0),
  flagPitch (false), pitch (0.f),
  flagSize  (false), size  (0.f),
  flagLoop  (false), loop  (0.f),
  flagTime  (false), time  (0.f),
  relative  (false), doppler(true), pan2D(false), occlusion(false), streaming(false),
  length(0), intent(SI_NONE), status(SS_STOPPED) {}

YSE::sound& YSE::sound::create(const char * fileName, channel * ch, Bool loop, Flt volume, Bool streaming) {
  if (pimpl) {
    INTERNAL::Global.getLog().emit(E_SOUND_OBJECT_IN_USE);
    return *this;
  }
  
  pimpl = INTERNAL::Global.getSoundManager().addImplementation(this);
  if (ch == NULL) ch = &INTERNAL::Global.getChannelManager().mainMix();

  if (pimpl->create(fileName, ch, loop, volume, streaming)) {
    INTERNAL::Global.getSoundManager().setup(pimpl);
  } else {
    pimpl->objectStatus = SIS_DELETE;
    pimpl = NULL;
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
  if (ch == NULL) pimpl->parent = INTERNAL::Global.getChannelManager().mainMix().pimpl;
  else  pimpl->parent = ch->pimpl;
  pimpl->fader.set(volume);
  // we'll have to get created to true somehow when dsp objects are implemented

  return *this;
}


YSE::sound::~sound() {
  if (pimpl != NULL) {
    pimpl->head = NULL;
    pimpl = NULL;
  }
}

Bool YSE::sound::isValid() {
  if (pimpl) return true;
  return false;
}

YSE::sound& YSE::sound::setPosition(const Vec &v) {
  posValue = v;
  flagPos = true;
  return (*this);
}

YSE::Vec YSE::sound::getPosition() {
  return posValue;
}

YSE::sound& YSE::sound::setSpread(Flt value) {
  Clamp(value, 0.f, 1.f);
  spread = value;
  flagSpread = true;
  return (*this);
}

Flt YSE::sound::getSpread() {
  return pimpl->spread;
}

YSE::sound& YSE::sound::setVolume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  volumeValue = value;
  volumeTime = time;
  flagVolume = true;
  return (*this);
}

Flt YSE::sound::getVolume() {
  return volumeValue;
}

YSE::sound& YSE::sound::setSpeed(Flt value) {
  pitch = value;
  flagPitch = true;
  return (*this);
}

Flt YSE::sound::getSpeed() {
  return pitch;
}

YSE::sound& YSE::sound::setSize(Flt value) {
  size = value;
  flagSize = true;
  return (*this);
}

Flt YSE::sound::getSize() {
  return size;
}

YSE::sound& YSE::sound::setLooping(Bool value) {
  loop = value;
  flagLoop = true;
  return (*this);
}

Bool YSE::sound::isLooping() {
  return loop;
}


YSE::sound& YSE::sound::play() {
  intent = SI_PLAY;
  return (*this);
}

YSE::sound& YSE::sound::pause() {
  intent = SI_PAUSE;
  return (*this);
}

YSE::sound& YSE::sound::stop() {
  intent = SI_STOP;
  return (*this);
}

YSE::sound& YSE::sound::toggle() {
  intent = SI_TOGGLE;
  return (*this);
}

YSE::sound& YSE::sound::restart() {
  intent = SI_RESTART;
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
  occlusion = value;
  return (*this);
}

Bool YSE::sound::getOcclusion() {
  return occlusion;
}

Bool YSE::sound::isStreaming() {
  return streaming;
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
  time = value;
  flagTime = true;
  return (*this);
}

Flt YSE::sound::getTime() {
  return time;
}

UInt YSE::sound::getLength() {
  return length;
}

YSE::sound& YSE::sound::setRelative(Bool value) {
  relative = value;
  return (*this);
}

Bool YSE::sound::isRelative() {
  return relative;
}

YSE::sound& YSE::sound::setDoppler(Bool value) {
  doppler = value;
  return (*this);
}

Bool YSE::sound::getDoppler() {
  return doppler;
}

YSE::sound& YSE::sound::set2D(Bool value) {
  relative = value;
  doppler = !value;
  pan2D = value;
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
  flagFade = true;
  fadeAndStopTime = time;

  return (*this);
}

YSE::sound& YSE::sound::moveTo(channel & target) {
  if (newChannel.load() != &target) {
    newChannel = &target;
    moveChannel = true;
  }

  return (*this);
}