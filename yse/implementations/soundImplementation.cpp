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
	_loading(true),
	parent(NULL),
  looping(false),
  _spread(0),
  
  isVirtual(false),
  source_dsp(NULL),
  post_dsp(NULL),
  _setPostDSP(false),
  _postDspPtr(NULL),

  _release(false),
  _occlusion(0.f),
  _occlusionActive(false),
  _streaming(false),
  _relative(false),
  _noDoppler(false),
  _pan2D(false),
  _fadeAndStop(false),
  link(NULL),
  
  _cCheck(1.0),
  _setVolume(false),
  _volumeValue(0),
  _volumeTime(0),
  _currentVolume(0),
  _pitch(1.f),
  _size(1.0f),
  _signalPlay(false),
  _signalPause(false),
  _signalStop(false),
  _signalToggle(false),
  newFilePos(0),
  currentFilePos(0),
  setFilePos(false),
  _length(0),
  _setFadeAndStop(false),
  _fadeAndStopTime(0),
  bufferVolume(0),
  startOffset(0),
  stopOffset(0),
  file(NULL){

  _fader.set(0.5f); 
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
    intent = SS_STOPPED;

    if (file->create(ioFile)) {
      return true;
    }
    else {
      file->release();
      file = NULL;      
    }
    return false;
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
  filePtr = NULL;
  newPos.zero();
  lastPos.zero();
  vel.zero();
  velocity = 0.0f;
}

void YSE::INTERNAL::soundImplementation::update() {
  if (_loading) {
    if (file->state() == READY) {
      filebuffer.resize(file->channels());
      buffer = &filebuffer;
      lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
      for (UInt i = 0; i < lastGain.size(); i++) {
        lastGain[i].resize(buffer->size(), 0.0f);
      }
      
      _loading = false;
    }
    else {
      return; // do not update as long as file is loading
    }
  }

  // position
  newPos.x = _pos.x.get() * Global.getSettings().distanceFactor;
  newPos.y = _pos.y.get() * Global.getSettings().distanceFactor;
  newPos.z = _pos.z.get() * Global.getSettings().distanceFactor;

  // distance to listener
  if (_relative) {
    distance = Dist(Vec(0), newPos);
  }
  else {
    distance = Dist(newPos, Global.getListener().newPos);
  }
  virtualDist = (distance - _size) * (1 / (_fader.getValue() > 0.000001f ? _fader.getValue() : 0.000001f));
  if (parent->allowVirtualSounds()) isVirtual = true;
  else isVirtual = false;

  // doppler effect
  if (_noDoppler) velocity = 0;
  else {
    vel = (newPos - lastPos) * (1 / Global.getTime().delta());
    
    Vec listenerVelocity;
    listenerVelocity.x = Global.getListener().vel.x.get();
    listenerVelocity.y = Global.getListener().vel.y.get();
    listenerVelocity.z = Global.getListener().vel.z.get();

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

  // angle
  Vec dir = _relative ? newPos : newPos - Global.getListener().newPos;
  if (_relative) angle = -atan2(dir.x, dir.z);
  else angle = (atan2(dir.z, dir.x) - atan2(Global.getListener().forward.z.get(), Global.getListener().forward.x.get()));
  while (angle > Pi) angle -= Pi2;
  while (angle < -Pi) angle += Pi2;
  angle = -angle;

  // occlusion
  if (System().occlusionCallback() != NULL && _occlusionActive) {
    _occlusion = System().occlusionCallback()(newPos, Global.getListener().newPos);
    Clamp(_occlusion, 0.f, 1.f);
  }

  // post dsp
  if (_setPostDSP) {
    addDSP(*_postDspPtr);
    _setPostDSP = false;
  }
}

Bool YSE::INTERNAL::soundImplementation::dsp() {
  //if (_cCheck != 1.0) return false;
  if (_loading) return false;

  // this is probably not needed
  /*if (filebuffer[0].getLength() > BUFFERSIZE) {
  Error.emit(E_SOUND_WRONG);
  _status = SS_STOPPED;
  _release = true;
  return false;
  }*/

  // handle signals
  if (_signalRestart) {
    //filePtr = 0;
    //if (_streaming) file->needsReset = true;
    intent = SS_WANTSTORESTART;
    _signalRestart = false;
  }

  if (_signalPlay) {
    if (intent != SS_PLAYING && intent != SS_PLAYING_FULL_VOLUME) intent = SS_WANTSTOPLAY;
    _signalPlay = false;
  }

  if (_signalPause) {
    if (intent != SS_STOPPED && intent != SS_PAUSED) intent = SS_WANTSTOPAUSE;
    _signalPause = false;
  }

  if (_signalStop) {
    if (intent != SS_STOPPED && intent != SS_PAUSED) {
      intent = SS_WANTSTOSTOP;
      //if (_streaming) file->needsReset = true;
    }
    else if (intent == SS_PAUSED) {
      intent = SS_STOPPED;
      filePtr = 0;
      if (_streaming) file->reset();
    }
    _signalStop = false;
  }

  if (_signalToggle) {
    if (intent == SS_PLAYING || intent == SS_WANTSTOPLAY || intent == SS_PLAYING_FULL_VOLUME) intent = SS_WANTSTOPAUSE;
    else intent = SS_WANTSTOPLAY;
    _signalToggle = false;
  }

  if (intent == SS_STOPPED || intent == SS_PAUSED) return false;
  if (isVirtual) return false;

  // volume update
  if (_setVolume) {
    _fader.set(_volumeValue, (Int)_volumeTime);
    _setVolume = false;
  }
  if (_setFadeAndStop) {
    _fader.set(0, (Int)_fadeAndStopTime);
    _fadeAndStop = true;
    _setFadeAndStop = false;
  }

  _currentVolume = _fader.getValue();

  // change position if filePtrguard  is changed
  if (setFilePos) {
    Clamp(newFilePos, 0.f, static_cast<Flt>(file->length()));
    filePtr = newFilePos;
    setFilePos = false;
  }

  if (source_dsp != NULL) {
    source_dsp->frequency(_pitch);
    // TODO: we need to think about what we're gonna do with latency and softsynts
    Int latency = 0;
    source_dsp->process(intent, latency);

  }
  else if (file->read(filebuffer, filePtr, STANDARD_BUFFERSIZE, _pitch + velocity, looping, intent, bufferVolume) == false) {
    // non looping sound has reached end of file
    /*filePtr = 0;
    _status = SS_STOPPED;
    if (_streaming) file->needsReset = true;*/
  }

  // update file position for query by frontend
  currentFilePos = filePtr;

  // shorthand function to stop after fade out
  if (_fadeAndStop && (_fader.getValue() == 0)) {
    _signalStop = true;
    _fadeAndStop = false;
  }

  // update fader
  _fader.update();

  // apply dsp's if needed
  if (post_dsp != NULL) {
    DSP::dspObject * ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer);
      ptr = ptr->link();
    }
  }
  return true;
}

void YSE::INTERNAL::soundImplementation::calculateGain(Int channel, Int source) {
  Flt finalGain = parent->outConf[channel].finalGain;
  if (lastGain[channel][source] == finalGain) {
    channelBuffer *= (finalGain);
    return;
  }
  Flt length = 50;
  Clamp(length, 1.f, static_cast<Flt>(channelBuffer.getLength()));
  Flt step = (finalGain - lastGain[channel][source]) / static_cast<Flt>(length);
  Flt multiplier = lastGain[channel][source];
  Flt * ptr = channelBuffer.getBuffer();
  for (UInt i = 0; i < length; i++) {
    *ptr++ *= (multiplier);
    multiplier += step;
  }
  UInt leftOvers = channelBuffer.getLength() - (UInt)length;
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
  lastGain[channel][source] = finalGain;
}

void YSE::INTERNAL::soundImplementation::toChannels() {
#pragma warning ( disable : 4258 )
  for (UInt x = 0; x < buffer->size(); x++) {
    // calculate spread value for multichannel sounds
    Flt spreadAdjust = 0;
    if (buffer->size() > 1) spreadAdjust = (((2 * Pi / buffer->size()) * x) + (Pi / buffer->size()) - Pi) * _spread;

    // initial panning
    for (UInt i = 0; i < parent->outConf.size(); i++) {
      parent->outConf[i].initPan = (1 + cos(parent->outConf[i].angle - (angle + spreadAdjust))) * 0.5f;
      parent->outConf[i].effective = 0;
      // effective speakers
      for (UInt j = 0; j < parent->outConf.size(); j++) {
        parent->outConf[i].effective += (1 + cos(parent->outConf[i].angle - parent->outConf[j].angle) * 0.5f);
      }
      // initial gain
      parent->outConf[i].initGain = parent->outConf[i].initPan / parent->outConf[i].effective;
    }
    // emitted power
    Flt power = 0;
    for (UInt i = 0; i < parent->outConf.size(); i++) {
      power += pow(parent->outConf[i].initGain, 2);
    }
    // calculated power
    Flt dist = distance - _size;
    if (dist < 0) dist = 0;
    Flt correctPower = 1 / pow(dist, (2 * Global.getSettings().rolloffScale));
    if (correctPower > 1) correctPower = 1;

    // final gain assignment
    for (UInt j = 0; j < parent->out.size(); ++j) {
      parent->outConf[j].ratio = pow(parent->outConf[j].initGain, 2) / power;
      channelBuffer = (*buffer)[x];
      parent->outConf[j].finalGain = sqrt(correctPower * parent->outConf[j].ratio);

      // add volume control now
      if (_occlusionActive) parent->outConf[j].finalGain *= 1 - _occlusion;
      calculateGain(j, x);
      channelBuffer *= _fader();
      parent->out[j] += channelBuffer;
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
  intent = SS_STOPPED;
  buffer = &source_dsp->buffer;
  _loading = false; // dsp source does not have to load
  lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
  for (UInt i = 0; i < lastGain.size(); i++) {
    lastGain[i].resize(buffer->size(), 0.0f);
  }
}

bool YSE::INTERNAL::soundImplementation::sortSoundObjects(const soundImplementation & lhs, const soundImplementation & rhs) {
  if (!lhs.parent->allowVirtual) return true;
  if (!rhs.parent->allowVirtual) return false;
  return (lhs.virtualDist > rhs.virtualDist);
}



