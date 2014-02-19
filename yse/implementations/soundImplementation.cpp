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


YSE::INTERNAL::soundImplementation::soundImplementation() :
  loading_dsp(true),
  parent_dsp(NULL),
  looping_dsp(false),
  spread_dsp(0),
  
  isVirtual_dsp(false),
  source_dsp(NULL),
  post_dsp(NULL),
  _setPostDSP(false),
  _postDspPtr(NULL),

  _release(false),
  occlusion_dsp(0.f),
  _occlusionActive(false),
  streaming_dsp(false),
  _relative(false),
  _noDoppler(false),
  _pan2D(false),
  fadeAndStop_dsp(false),
  link(NULL),
  
  _cCheck(1.0),
  setVolume_dsp(false),
  volumeValue_dsp(0),
  volumeTime_dsp(0),
  currentVolume_dsp(0),
  pitch_dsp(1.f),
  size_dsp(1.0f),
  signalPlay_dsp(false),
  signalPause_dsp(false),
  signalStop_dsp(false),
  signalToggle_dsp(false),
  newFilePos_dsp(0),
  currentFilePos_dsp(0),
  setFilePos_dsp(false),
  _length(0),
  setFadeAndStop_dsp(false),
  fadeAndStopTime_dsp(0),
  bufferVolume_dsp(0),
  startOffset(0),
  stopOffset(0),
  file(NULL){

  fader_dsp.set(0.5f);
  _pos.x = 0.f;
  _pos.y = 0.f;
  _pos.z = 0.f;

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

  if (link) *link = NULL;
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

void YSE::INTERNAL::soundImplementation::initialize() {
  filePtr_dsp = NULL;
  newPos.zero();
  lastPos.zero();
  vel.zero();
  velocity_dsp = 0.0f;
}

void YSE::INTERNAL::soundImplementation::update() {
  if (loading_dsp) {
    if (file->state() == READY) {
      filebuffer_dsp.resize(file->channels());
      buffer_dsp = &filebuffer_dsp;
      lastGain_dsp.resize(Global.getChannelManager().getNumberOfOutputs());
      for (UInt i = 0; i < lastGain_dsp.size(); i++) {
        lastGain_dsp[i].resize(buffer_dsp->size(), 0.0f);
      }
      
      loading_dsp = false;
    }
    else {
      return; // do not update as long as file is loading
    }
  }

  // position
  newPos.x = _pos.x * Global.getSettings().distanceFactor;
  newPos.y = _pos.y * Global.getSettings().distanceFactor;
  newPos.z = _pos.z * Global.getSettings().distanceFactor;

  // distance to listener
  if (_relative) {
    distance_dsp = Dist(Vec(0), newPos);
  }
  else {
    distance_dsp = Dist(newPos, Global.getListener().newPos);
  }
  virtualDist = (distance_dsp - size_dsp) * (1 / (fader_dsp.getValue() > 0.000001f ? fader_dsp.getValue() : 0.000001f));
  if (parent_dsp->allowVirtualSounds()) isVirtual_dsp = true;
  else isVirtual_dsp = false;

  // doppler effect
  Flt velocity = velocity_dsp; // avoid using atomic all the time
  if (_noDoppler) velocity_dsp = 0;
  else {
    vel = (newPos - lastPos) * (1 / Global.getTime().delta());
    
    Vec listenerVelocity;
    listenerVelocity.x = Global.getListener().vel.x.load();
    listenerVelocity.y = Global.getListener().vel.y.load();
    listenerVelocity.z = Global.getListener().vel.z.load();

    
    if (vel == Vec(0) && listenerVelocity == Vec(0)) velocity = 0;
    else {
      Vec dist = _relative ? newPos : newPos - Global.getListener().newPos;
      if (dist != Vec(0)) {
        Flt rSound = Dot(vel, dist) / dist.length();
        Flt rList = Dot(listenerVelocity, dist) / dist.length();
        velocity = 1 - (440 / (((344.0f + rList) / (344.0f + rSound)) * 440));
        velocity *= Global.getSettings().dopplerScale;
      }
    }
    
  }
  lastPos = newPos;
  // disregard rounding errors
  if (abs(velocity) < 0.01f) velocity = 0.0f;
  velocity_dsp = velocity; // back to atomic

  // angle
  Flt angle = angle_dsp; // avoid using atomic all the time
  Vec dir = _relative ? newPos : newPos - Global.getListener().newPos;
  if (_relative) angle = -atan2(dir.x, dir.z);
  else angle = (atan2(dir.z, dir.x) - atan2(Global.getListener().forward.z.load(), Global.getListener().forward.x.load()));
  while (angle > Pi) angle -= Pi2;
  while (angle < -Pi) angle += Pi2;
  angle = -angle;
  angle_dsp = angle; // back to atomic

  // occlusion
  if (System().occlusionCallback() != NULL && _occlusionActive) {
    occlusion_dsp = System().occlusionCallback()(newPos, Global.getListener().newPos);
    Clamp(occlusion_dsp, 0.f, 1.f);
  }

  // post dsp
  if (_setPostDSP) {
    addDSP(*_postDspPtr);
    _setPostDSP = false;
  }
  
}

Bool YSE::INTERNAL::soundImplementation::dsp() {
  //if (_cCheck != 1.0) return false;
  if (loading_dsp) return false;

  // this is probably not needed
  /*if (filebuffer[0].getLength() > BUFFERSIZE) {
  Error.emit(E_SOUND_WRONG);
  _status = SS_STOPPED;
  _release = true;
  return false;
  }*/

  // handle signals
  if (signalRestart_dsp) {
    //filePtr = 0;
    //if (_streaming) file->needsReset = true;
    intent_dsp = SS_WANTSTORESTART;
    signalRestart_dsp = false;
  }

  if (signalPlay_dsp) {
    if (intent_dsp != SS_PLAYING && intent_dsp != SS_PLAYING_FULL_VOLUME) intent_dsp = SS_WANTSTOPLAY;
    signalPlay_dsp = false;
  }

  if (signalPause_dsp) {
    if (intent_dsp != SS_STOPPED && intent_dsp != SS_PAUSED) intent_dsp = SS_WANTSTOPAUSE;
    signalPause_dsp = false;
  }

  if (signalStop_dsp) {
    if (intent_dsp != SS_STOPPED && intent_dsp != SS_PAUSED) {
      intent_dsp = SS_WANTSTOSTOP;
      //if (_streaming) file->needsReset = true;
    }
    else if (intent_dsp == SS_PAUSED) {
      intent_dsp = SS_STOPPED;
      filePtr_dsp = 0;
      if (streaming_dsp) file->reset();
    }
    signalStop_dsp = false;
  }

  if (signalToggle_dsp) {
    if (intent_dsp == SS_PLAYING || intent_dsp == SS_WANTSTOPLAY || intent_dsp == SS_PLAYING_FULL_VOLUME) intent_dsp = SS_WANTSTOPAUSE;
    else intent_dsp = SS_WANTSTOPLAY;
    signalToggle_dsp = false;
  }

  if (intent_dsp == SS_STOPPED || intent_dsp == SS_PAUSED) return false;
  if (isVirtual_dsp) return false;

  // volume update
  if (setVolume_dsp) {
    fader_dsp.set(volumeValue_dsp, (Int)volumeTime_dsp);
    setVolume_dsp = false;
  }
  if (setFadeAndStop_dsp) {
    fader_dsp.set(0, (Int)fadeAndStopTime_dsp);
    fadeAndStop_dsp = true;
    setFadeAndStop_dsp = false;
  }

  currentVolume_dsp = fader_dsp.getValue();

  // change position if filePtrguard  is changed
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

  // shorthand function to stop after fade out
  if (fadeAndStop_dsp && (fader_dsp.getValue() == 0)) {
    signalStop_dsp = true;
    fadeAndStop_dsp = false;
  }

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



