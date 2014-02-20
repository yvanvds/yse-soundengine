/*
  ==============================================================================

    soundImplementation.cpp
    Created: 28 Jan 2014 11:50:52am
    Author:  yvan

  ==============================================================================
*/

#include "soundImplementation.h"
#include "channelImplementation.h"
#include "../internal/global.h"
#include "../implementations/logImplementation.h"
#include "../implementations/listenerImplementation.h"
#include "../internal/time.h"
#include "../internal/settings.h"
#include "../internal/soundManager.h"
#include "../internal/channelManager.h"
#include "../utils/misc.hpp"
#include "../system.hpp"
#include "../sound.hpp"


YSE::INTERNAL::soundImplementation::soundImplementation() :
  loading(true),
  parent_dsp(NULL),
  looping_dsp(false),
  spread(0),
  head(NULL),
  isVirtual_dsp(false),
  source_dsp(NULL),
  post_dsp(NULL),
  _setPostDSP(false),
  _postDspPtr(NULL),

  release(false),
  occlusion_dsp(0.f),
  occlusionActive(false),
  streaming_dsp(false),
  relative(false),
  doppler(true),
  
  _cCheck(1.0),
  setVolume(false),
  volumeValue(0),
  volumeTime(0),
  currentVolume_dsp(0),
  pitch_dsp(1.f),
  size_dsp(1.0f),
  headIntent(SI_NONE),
  newFilePos_dsp(0),
  currentFilePos_dsp(0),
  setFilePos_dsp(false),
  _length(0),
  setFadeAndStop(false),
  stopAfterFade(false),
  fadeAndStopTime(0),
  bufferVolume_dsp(0),
  startOffset(0),
  stopOffset(0),
  file(NULL){

  fader.set(0.5f);
  pos.zero();

#if defined YSE_DEBUG
  Global.getLog().emit(E_SOUND_ADDED);
#endif
}

YSE::INTERNAL::soundImplementation::~soundImplementation() {
  // only streams should delete their source. Other files are shared.
  if (file != NULL) {
    file->release();
  }

#if defined YSE_DEBUG
  Global.getLog().emit(E_SOUND_DELETED);
#endif
  if (post_dsp && post_dsp->calledfrom) post_dsp->calledfrom = NULL;
}

bool YSE::INTERNAL::soundImplementation::create(const std::string &fileName, Bool stream) {
  File ioFile;
  ioFile = File::getCurrentWorkingDirectory().getChildFile(juce::String(fileName));

  if (!ioFile.existsAsFile()) {
    Global.getLog().emit(E_FILE_ERROR, "file not found for " + ioFile.getFullPathName().toStdString());
    return false;
  }

  if (!stream) {
    file = Global.getSoundManager().add(ioFile);
    intent_dsp = SS_STOPPED;

    if (file == NULL) {
      return false;
    } else {
      return true;
    }
  }

  //else {
    // streams have their own soundfile
    //_streaming = true;
    //intent = SS_STOPPED;
    //file = new soundFile;
    //if (file->create(ioFile, true)) {
    //  filebuffer.resize(file->channels());
    //  buffer = &filebuffer;
    //  _length = file->length();
    //  return true;
    //}
    //else {
    //  delete file;
    //  file = NULL;
    //  return false;
    //}
  //}
  return false;
}

void YSE::INTERNAL::soundImplementation::initialize(sound * head) {
  this->head = head;
  filePtr_dsp = NULL;
  newPos.zero();
  lastPos.zero();
  velocityVec.zero();
  velocity = 0.0f;
}

void YSE::INTERNAL::soundImplementation::sync() {
  if (loading) return;

  if (head == NULL) {
    // sound head is destructed, so stop and remove
    if (intent_dsp != SS_STOPPED || SS_WANTSTOSTOP) {
      intent_dsp = SS_WANTSTOSTOP;
    }
    else if (intent_dsp == SS_STOPPED) {
      release = true;
    }
    return;
  }

  // get new values from head
  if (head->flagPos) {
    pos = head->posValue;
    head->flagPos = false;
  }

  if (head->flagSpread) {
    spread = head->spread;
    head->flagSpread = false;
  }

  if (head->flagFade) {
    setFadeAndStop = true;
    fadeAndStopTime = head->fadeAndStopTime;
    head->flagFade = false;
  }

  if (head->flagVolume) {
    setVolume = true;
    volumeValue = head->volumeValue;
    volumeTime = head->volumeTime;
    head->flagVolume = false;
  }

  if (head->flagPitch) {
    pitch_dsp = head->pitch;
    head->flagPitch = false;
  }

  if (head->flagSize) {
    size_dsp = head->size;
    head->flagSize = false;
  }

  if (head->flagLoop) {
    looping_dsp = head->loop;
    head->flagLoop = false;
  }

  if (head->flagTime) {
    newFilePos_dsp = head->time;
    setFilePos_dsp = true;
    head->flagTime = false;
  }


  relative = head->relative;
  doppler = head->doppler;
  occlusionActive = head->occlusion;
  headIntent = head->intent;
  head->intent = SI_NONE;

  // sync dsp values
  currentVolume_upd = currentVolume_dsp;

  // copy updated values to head
  head->volumeValue = currentVolume_upd;
  head->time = currentFilePos_dsp;
}

void YSE::INTERNAL::soundImplementation::update() {
  ///////////////////////////////////////////
  // setup sound when loaded
  ///////////////////////////////////////////
  if (loading) {
    if (file->state() == READY) {
      filebuffer_dsp.resize(file->channels());
      buffer_dsp = &filebuffer_dsp;
      lastGain_dsp.resize(Global.getChannelManager().getNumberOfOutputs());
      for (UInt i = 0; i < lastGain_dsp.size(); i++) {
        lastGain_dsp[i].resize(buffer_dsp->size(), 0.0f);
      }
      
      loading = false;
    }
    else {
      return; // do not update as long as file is loading
    }
  }

  ///////////////////////////////////////////
  // set position and distance
  ///////////////////////////////////////////
  newPos = pos * Global.getSettings().distanceFactor;

  // distance to listener
  if (relative) {
    distance_dsp = Dist(Vec(0), newPos);
  }
  else {
    distance_dsp = Dist(newPos, Global.getListener().newPos);
  }
  virtualDist = (distance_dsp - size_dsp) * (1 / (currentVolume_upd > 0.000001f ? currentVolume_upd : 0.000001f));
  if (parent_dsp->allowVirtualSounds()) isVirtual_dsp = true;
  else isVirtual_dsp = false;

  ///////////////////////////////////////////
  // calculate doppler effect 
  ///////////////////////////////////////////
  Flt vel = velocity; // avoid using atomic all the time
  if (!doppler) vel = 0;
  else {
    velocityVec = (newPos - lastPos) * (1 / Global.getTime().delta());
    
    Vec listenerVelocity;
    listenerVelocity.x = Global.getListener().vel.x.load();
    listenerVelocity.y = Global.getListener().vel.y.load();
    listenerVelocity.z = Global.getListener().vel.z.load();

    if (velocityVec == Vec(0) && listenerVelocity == Vec(0)) vel = 0;
    else {
      Vec dist = relative ? newPos : newPos - Global.getListener().newPos;
      if (dist != Vec(0)) {
        Flt rSound = Dot(vel, dist) / dist.length();
        Flt rList = Dot(listenerVelocity, dist) / dist.length();
        vel = 1 - (440 / (((344.0f + rList) / (344.0f + rSound)) * 440));
        vel *= Global.getSettings().dopplerScale;
      }
    }
    
  }
  lastPos = newPos;
  // disregard rounding errors
  if (abs(vel) < 0.01f) vel = 0.0f;
  velocity = vel; // back to atomic

  ///////////////////////////////////////////
  // calculate angle
  ///////////////////////////////////////////
  Flt a = angle_dsp; // avoid using atomic all the time
  Vec dir = relative ? newPos : newPos - Global.getListener().newPos;
  if (relative) a = -atan2(dir.x, dir.z);
  else a = (atan2(dir.z, dir.x) - atan2(Global.getListener().forward.z.load(), Global.getListener().forward.x.load()));
  while (a > Pi) a -= Pi2;
  while (a < -Pi) a += Pi2;
  a = -a;
  angle_dsp = a; // back to atomic

  ///////////////////////////////////////////
  // sound occlusion (optional)
  ///////////////////////////////////////////
  if (System().occlusionCallback() != NULL && occlusionActive) {
    occlusion_dsp = System().occlusionCallback()(newPos, Global.getListener().newPos);
    Clamp(occlusion_dsp, 0.f, 1.f);
  }

  ///////////////////////////////////////////
  // dsp processing (optional)
  ///////////////////////////////////////////
  if (_setPostDSP) {
    addDSP(*_postDspPtr);
    _setPostDSP = false;
  }
  
}

Bool YSE::INTERNAL::soundImplementation::dsp() {
  if (loading) return false;

  ///////////////////////////////////////////
  // handle play status
  ///////////////////////////////////////////
  dspFunc_parseIntent();

  if (intent_dsp == SS_STOPPED || intent_dsp == SS_PAUSED) return false;
  if (isVirtual_dsp) return false;

  ///////////////////////////////////////////
  // set volume at sound level
  ///////////////////////////////////////////
  if (setVolume) {
    fader.set(volumeValue, (Int)volumeTime);
    setVolume = false;
  }
  if (setFadeAndStop) {
    fader.set(0, (Int)fadeAndStopTime);
    stopAfterFade = true;
    setFadeAndStop = false;
  }
  currentVolume_dsp = fader.getValue();

  if (stopAfterFade && currentVolume_dsp == 0) {
    stopAfterFade = false;
    intent_dsp = SS_STOPPED;
    return false;
  }

  ///////////////////////////////////////////
  // set position
  ///////////////////////////////////////////
  if (setFilePos_dsp) {
    Clamp(newFilePos_dsp, 0.f, static_cast<Flt>(file->length()));
    filePtr_dsp = newFilePos_dsp;
    setFilePos_dsp = false;
  }

  if (source_dsp != NULL) {
    source_dsp->frequency(pitch_dsp);
    // TODO: we need to think about what we're gonna do with latency and softsynts
    Int latency = 0;
    source_dsp->process(intent_dsp, latency);
  }

  else 
  if (file->read(filebuffer_dsp, filePtr_dsp, STANDARD_BUFFERSIZE, pitch_dsp + velocity_dsp, looping_dsp, intent_dsp, bufferVolume_dsp) == false) {
    // non looping sound has reached end of file
    /*filePtr = 0;
    _status = SS_STOPPED;
    if (_streaming) file->needsReset = true;*/
  }

  // update file position for query by frontend
  currentFilePos_dsp = filePtr_dsp;



  // update fader
  fader_dsp.update();

  // apply dsp's if needed
  if (post_dsp != NULL) {
    DSP::dspObject * ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer_dsp);
      ptr = ptr->link();
    }
  }
  return true;
}

void YSE::INTERNAL::soundImplementation::dspFunc_parseIntent() {
  switch (headIntent) {
    case SI_RESTART:
    {
      intent_dsp = SS_WANTSTORESTART;
      break;
    }

    case SI_PLAY:
    {
      if (intent_dsp != SS_PLAYING && intent_dsp != SS_PLAYING_FULL_VOLUME)  {
        intent_dsp = SS_WANTSTOPLAY;
      }
      break;
    }

    case SI_PAUSE:
    {
      if (intent_dsp != SS_STOPPED && intent_dsp != SS_PAUSED) {
        intent_dsp = SS_WANTSTOPAUSE;
      }
      break;
    }

    case SI_STOP:
    {
      if (intent_dsp != SS_STOPPED && intent_dsp != SS_PAUSED) {
        intent_dsp = SS_WANTSTOSTOP;
      }
      else if (intent_dsp == SS_PAUSED) {
        intent_dsp = SS_STOPPED;
        filePtr_dsp = 0;
        if (streaming_dsp) file->reset();
      }
    }

    case SI_TOGGLE:
    {
      if (intent_dsp == SS_PLAYING || intent_dsp == SS_WANTSTOPLAY || intent_dsp == SS_PLAYING_FULL_VOLUME) intent_dsp = SS_WANTSTOPAUSE;
      else intent_dsp = SS_WANTSTOPLAY;
    }
  }

  headIntent = SI_NONE;
}


void YSE::INTERNAL::soundImplementation::calculateGain(Int channel, Int source) {
  Flt finalGain = parent_dsp->outConf[channel].finalGain;
  if (lastGain_dsp[channel][source] == finalGain) {
    channelBuffer_dsp *= (finalGain);
    return;
  }
  Flt length = 50;
  Clamp(length, 1.f, static_cast<Flt>(channelBuffer_dsp.getLength()));
  Flt step = (finalGain - lastGain_dsp[channel][source]) / static_cast<Flt>(length);
  Flt multiplier = lastGain_dsp[channel][source];
  Flt * ptr = channelBuffer_dsp.getBuffer();
  for (UInt i = 0; i < length; i++) {
    *ptr++ *= (multiplier);
    multiplier += step;
  }
  UInt leftOvers = channelBuffer_dsp.getLength() - (UInt)length;
  for (; leftOvers > 7; leftOvers -= 8, ptr += 8) {
    ptr[0] *= finalGain;
    ptr[1] *= finalGain;
    ptr[2] *= finalGain;
    ptr[3] *= finalGain;
    ptr[4] *= finalGain;
    ptr[5] *= finalGain;
    ptr[6] *= finalGain;
    ptr[7] *= finalGain;
  }
  while (leftOvers--) *ptr++ *= finalGain;
  lastGain_dsp[channel][source] = finalGain;
}

void YSE::INTERNAL::soundImplementation::toChannels() {
#pragma warning ( disable : 4258 )
  for (UInt x = 0; x < buffer_dsp->size(); x++) {
    // calculate spread value for multichannel sounds
    Flt spreadAdjust = 0;
    if (buffer_dsp->size() > 1) spreadAdjust = (((2 * Pi / buffer_dsp->size()) * x) + (Pi / buffer_dsp->size()) - Pi) * spread_dsp;

    // initial panning
    for (UInt i = 0; i < parent_dsp->outConf.size(); i++) {
      parent_dsp->outConf[i].initPan = (1 + cos(parent_dsp->outConf[i].angle - (angle_dsp + spreadAdjust))) * 0.5f;
      parent_dsp->outConf[i].effective = 0;
      // effective speakers
      for (UInt j = 0; j < parent_dsp->outConf.size(); j++) {
        parent_dsp->outConf[i].effective += (1 + cos(parent_dsp->outConf[i].angle - parent_dsp->outConf[j].angle) * 0.5f);
      }
      // initial gain
      parent_dsp->outConf[i].initGain = parent_dsp->outConf[i].initPan / parent_dsp->outConf[i].effective;
    }
    // emitted power
    Flt power = 0;
    for (UInt i = 0; i < parent_dsp->outConf.size(); i++) {
      power += pow(parent_dsp->outConf[i].initGain, 2);
    }
    // calculated power
    Flt dist = distance_dsp - size_dsp;
    if (dist < 0) dist = 0;
    Flt correctPower = 1 / pow(dist, (2 * Global.getSettings().rolloffScale));
    if (correctPower > 1) correctPower = 1;

    // final gain assignment
    for (UInt j = 0; j < parent_dsp->out.size(); ++j) {
      parent_dsp->outConf[j].ratio = pow(parent_dsp->outConf[j].initGain, 2) / power;
      channelBuffer_dsp = (*buffer_dsp)[x];
      parent_dsp->outConf[j].finalGain = sqrt(correctPower * parent_dsp->outConf[j].ratio);

      // add volume control now
      if (_occlusionActive) parent_dsp->outConf[j].finalGain *= 1 - occlusion_dsp;
      calculateGain(j, x);
      channelBuffer_dsp *= fader_dsp();
      parent_dsp->out[j] += channelBuffer_dsp;
    }
  }
}

void YSE::INTERNAL::soundImplementation::addDSP(DSP::dspObject & ptr) {
  if (post_dsp) {
    if (post_dsp->calledfrom) {
      *(post_dsp->calledfrom) = NULL;
    }
  }
  post_dsp = &ptr;
  post_dsp->calledfrom = &post_dsp;
}

void YSE::INTERNAL::soundImplementation::addSourceDSP(DSP::dspSourceObject &ptr) {
  source_dsp = &ptr;
  intent_dsp = SS_STOPPED;
  buffer_dsp = &source_dsp->buffer;
  loading_dsp = false; // dsp source does not have to load
  lastGain_dsp.resize(Global.getChannelManager().getNumberOfOutputs());
  for (UInt i = 0; i < lastGain_dsp.size(); i++) {
    lastGain_dsp[i].resize(buffer_dsp->size(), 0.0f);
  }
}

bool YSE::INTERNAL::soundImplementation::sortSoundObjects(const soundImplementation & lhs, const soundImplementation & rhs) {
  if (!lhs.parent_dsp->allowVirtual) return true;
  if (!rhs.parent_dsp->allowVirtual) return false;
  return (lhs.virtualDist > rhs.virtualDist);
}



