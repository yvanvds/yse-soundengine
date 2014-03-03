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


YSE::INTERNAL::soundImplementation::soundImplementation(sound * head) :
  objectStatus(SIS_CONSTRUCTED),
  parent(NULL),
  looping(false),
  spread(0),
  isVirtual(true),
  source_dsp(NULL),
  post_dsp(NULL),
  _setPostDSP(false),
  _postDspPtr(NULL),
  occlusion_dsp(0.f),
  occlusionActive(false),
  streaming_dsp(false),
  relative(false),
  doppler(true),
  setVolume(false),
  volumeValue(0),
  volumeTime(0),
  currentVolume_dsp(0),
  pitch(1.f),
  size(1.0f),
  headIntent(SI_NONE),
  newFilePos(0),
  currentFilePos(0),
  setFilePos(false),
  setFadeAndStop(false),
  stopAfterFade(false),
  fadeAndStopTime(0),
  bufferVolume(0),
  startOffset(0),
  stopOffset(0),
  file(NULL),
  head(head),
  filePtr(NULL),
  newPos(0.f),
  lastPos(0.f),
  velocityVec(0.f),
  velocity(0.f),
  pos(0.f)
  {
  fader.set(0.5f);

#if defined YSE_DEBUG
  Global.getLog().emit(E_SOUND_ADDED);
#endif
}

YSE::INTERNAL::soundImplementation::~soundImplementation() {
  // only streams should delete their source. Other files are shared.
  if (file != NULL) {
    file->release();
  }
  if (post_dsp && post_dsp->calledfrom) post_dsp->calledfrom = NULL;
}

bool YSE::INTERNAL::soundImplementation::create(const std::string &fileName, const channel * const ch, Bool loop, Flt volume, Bool streaming) {
  parent = ch->pimpl;
  looping = loop;
  fader.set(volume);

  File ioFile;
  ioFile = File::getCurrentWorkingDirectory().getChildFile(juce::String(fileName));

  if (!ioFile.existsAsFile()) {
    Global.getLog().emit(E_FILE_ERROR, "file not found for " + ioFile.getFullPathName().toStdString());
    return false;
  }

  if (!streaming) {
    file = Global.getSoundManager().add(ioFile);
    status_dsp = SS_STOPPED;
    status_upd = SS_STOPPED;

    if (file == NULL) {
      return false;
    } else {
      objectStatus = SIS_CREATED;
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

void YSE::INTERNAL::soundImplementation::setup() {
  if (objectStatus == SIS_CREATED) {
    if (file->state() == READY) {
      filebuffer.resize(file->channels());
      buffer = &filebuffer;
      lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
      head->length = file->length();
      for (UInt i = 0; i < lastGain.size(); i++) {
        lastGain[i].resize(buffer->size(), 0.0f);
      }

      objectStatus = SIS_LOADED;
    }
  }
}

Bool YSE::INTERNAL::soundImplementation::readyCheck() {
  if (objectStatus == SIS_LOADED) {
    // make sure the number of is not changed since setup
    if (lastGain.size() == Global.getChannelManager().getNumberOfOutputs()) {
      objectStatus = SIS_READY;
      return true;
    }
    else {
      // setup has changed, return to loading stage
      objectStatus = SIS_CREATED;
    }
  }
  return false;
}

void YSE::INTERNAL::soundImplementation::sync() {
  if (head == NULL) {
    objectStatus = SIS_DONE;

    // sound head is destructed, so stop and remove
    if (status_dsp != SS_STOPPED || SS_WANTSTOSTOP) {
      status_dsp = SS_WANTSTOSTOP;
    }
    else if (status_dsp == SS_STOPPED) {
      objectStatus = SIS_RELEASE;
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
    fadeAndStopTime = static_cast<Flt>(head->fadeAndStopTime);
    head->flagFade = false;
  }

  if (head->flagVolume) {
    setVolume = true;
    volumeValue = head->volumeValue;
    volumeTime = static_cast<Flt>(head->volumeTime);
    head->flagVolume = false;
  }

  if (head->flagPitch) {
    pitch = head->pitch;
    head->flagPitch = false;
  }

  if (head->flagSize) {
    size = head->size;
    head->flagSize = false;
  }

  if (head->flagLoop) {
    looping = head->loop;
    head->flagLoop = false;
  }

  if (head->flagTime) {
    newFilePos = head->time;
    setFilePos = true;
    head->flagTime = false;
  }

  if (head->moveChannel) {
    head->newChannel.load()->pimpl->add(this);
    head->moveChannel = false;
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
  head->time = currentFilePos;
  head->status = status_dsp;
  status_upd = status_dsp;
}

void YSE::INTERNAL::soundImplementation::update() {
  ///////////////////////////////////////////
  // set position and distance
  ///////////////////////////////////////////
  newPos = pos * Global.getSettings().distanceFactor;

  // distance to listener
  if (relative) {
    distance = Dist(Vec(0), newPos);
  }
  else {
    distance = Dist(newPos, Global.getListener().newPos);
  }
  virtualDist = (distance- size) * (1 / (currentVolume_upd > 0.000001f ? currentVolume_upd : 0.000001f));
  if (parent->allowVirtualSounds()) isVirtual = true;
  else isVirtual = false;

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
  Flt a = angle; // avoid using atomic all the time
  Vec dir = relative ? newPos : newPos - Global.getListener().newPos;
  if (relative) a = -atan2(dir.x, dir.z);
  else a = (atan2(dir.z, dir.x) - atan2(Global.getListener().forward.z.load(), Global.getListener().forward.x.load()));
  while (a > Pi) a -= Pi2;
  while (a < -Pi) a += Pi2;
  a = -a;
  angle = a; // back to atomic

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
  ///////////////////////////////////////////
  // handle play status
  ///////////////////////////////////////////
  dspFunc_parseIntent();

  if (status_dsp == SS_STOPPED || status_dsp == SS_PAUSED) return false;
  if (isVirtual) return false;

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
    status_dsp = SS_STOPPED;
    return false;
  }

  ///////////////////////////////////////////
  // set position
  ///////////////////////////////////////////
  if (setFilePos) {
    Clamp(newFilePos, 0.f, static_cast<Flt>(file->length()));
    filePtr = newFilePos;
    setFilePos = false;
  }

  ///////////////////////////////////////////
  // fill buffer
  ///////////////////////////////////////////
  if (source_dsp != NULL) {
    source_dsp->frequency(pitch);
    // TODO: we need to think about what we're gonna do with latency and softsynts
    Int latency = 0;
    source_dsp->process(status_dsp, latency);
  } else 
  if (file->read(filebuffer, filePtr, STANDARD_BUFFERSIZE, pitch + velocity, looping, status_dsp, bufferVolume) == false) {
    // non looping sound has reached end of file
    /*filePtr = 0;
    _status = SS_STOPPED;
    if (_streaming) file->needsReset = true;*/
  }

  // update file position for query by frontend
  currentFilePos = filePtr;

  // update fader
  fader.update();

  ///////////////////////////////////////////
  // apply post dsp if needed
  ///////////////////////////////////////////
  if (post_dsp != NULL) {
    DSP::dspObject * ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer);
      ptr = ptr->link();
    }
  }
  return true;
}

void YSE::INTERNAL::soundImplementation::dspFunc_parseIntent() {
  switch (headIntent) {
    case SI_RESTART:
    {
      status_dsp  = SS_WANTSTORESTART;
      break;
    }

    case SI_PLAY:
    {
      if (status_dsp  != SS_PLAYING && status_dsp  != SS_PLAYING_FULL_VOLUME)  {
        status_dsp = SS_WANTSTOPLAY;
      }
      break;
    }

    case SI_PAUSE:
    {
      if (status_dsp  != SS_STOPPED && status_dsp  != SS_PAUSED) {
        status_dsp = SS_WANTSTOPAUSE;
      }
      break;
    }

    case SI_STOP:
    {
      if (status_dsp != SS_STOPPED && status_dsp != SS_PAUSED) {
        status_dsp = SS_WANTSTOSTOP;
      }
      else if (status_dsp == SS_PAUSED) {
        status_dsp = SS_STOPPED;
        filePtr = 0;
        if (streaming_dsp) file->reset();
      }
    }

    case SI_TOGGLE:
    {
      if (status_dsp  == SS_PLAYING || status_dsp  == SS_WANTSTOPLAY || status_dsp  == SS_PLAYING_FULL_VOLUME) status_dsp  = SS_WANTSTOPAUSE;
      else status_dsp = SS_WANTSTOPLAY;
    }
  }

  headIntent = SI_NONE;
}


void YSE::INTERNAL::soundImplementation::dspFunc_calculateGain(Int channel, Int source) {
  Flt finalGain = parent->outConf[channel].finalGain;
  if (lastGain[channel][source] == finalGain) {
    channelBuffer *= (finalGain);
    return;
  }

  Flt length = 50;
  Clamp(length, 1.f, static_cast<Flt>(channelBuffer.getLength()));
  Flt step = (finalGain - lastGain[channel][source]) / length;
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
    if (buffer->size() > 1) spreadAdjust = (((2 * Pi / buffer->size()) * x) + (Pi / buffer->size()) - Pi) * spread;

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
    Flt dist = distance - size;
    if (dist < 0) dist = 0;
    Flt correctPower = 1 / pow(dist, (2 * Global.getSettings().rolloffScale));
    if (correctPower > 1) correctPower = 1;

    // final gain assignment
    for (UInt j = 0; j < parent->out.size(); ++j) {
      parent->outConf[j].ratio = pow(parent->outConf[j].initGain, 2) / power;
      channelBuffer = (*buffer)[x];
      parent->outConf[j].finalGain = sqrt(correctPower * parent->outConf[j].ratio);

      // add volume control now
      if (occlusionActive) parent->outConf[j].finalGain *= 1 - occlusion_dsp;
      dspFunc_calculateGain(j, x);
      channelBuffer *= fader();
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
  status_dsp = SS_STOPPED;
  buffer = &source_dsp->buffer;
  lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
  for (UInt i = 0; i < lastGain.size(); i++) {
    lastGain[i].resize(buffer->size(), 0.0f);
  }
}

bool YSE::INTERNAL::soundImplementation::sortSoundObjects(soundImplementation * lhs, soundImplementation * rhs) {
  if (!lhs->parent->allowVirtual) return true;
  if (!rhs->parent->allowVirtual) return false;
  return (lhs->virtualDist > rhs->virtualDist);
}

bool YSE::INTERNAL::soundImplementation::canBeDeleted(const soundImplementation & impl) {
  return impl.objectStatus == SIS_DELETE;
}

bool YSE::INTERNAL::soundImplementation::canBeRemovedFromLoading(const std::atomic<soundImplementation*> & elm) {
  return elm.load()->objectStatus >= SIS_READY;
}

