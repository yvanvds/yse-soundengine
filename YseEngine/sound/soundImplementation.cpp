/*
  ==============================================================================

    soundImplementation.cpp
    Created: 28 Jan 2014 11:50:52am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "../utils/fileFunctions.hpp"
#include "../patcher/patcherImplementation.h"

YSE::SOUND::implementationObject::implementationObject(sound * head) :
	_head_streaming(false),
	_head_length(0),
	_head_time(0.f),
	_head_status(SS_STOPPED),
	file(nullptr),
	bufferVolume(0),
	filePtr(0.f),
	newFilePos(0),
	currentFilePos(0),
	setFilePos(false),
	headIntent(SI_NONE),
	pos(0.0f),
	newPos(0.f),
	lastPos(0.f),
	velocityVec(0.f),
	velocity(0.f),
	pitch(1.f),
	size(1.0f),
	setVolume(false),
	volumeValue(0),
	volumeTime(0),
	setFadeAndStop(false),
	fadeAndStopTime(0),
	stopAfterFade(false),
	currentVolume_dsp(0),
	looping(false),
	relative(false),
	doppler(true),
	occlusionActive(false),
	occlusion_dsp(0.f),
	spread(0),
	patcher(nullptr),
	source_dsp(nullptr),
	_setPostDSP(false),
	_postDspPtr(nullptr),
	post_dsp(nullptr),
	parent(nullptr),
	startOffset(0),
	stopOffset(0),
	streaming(false),
	head(head),
	objectStatus(OBJECT_CONSTRUCTED)
  {
  fader.set(0.5f);

#if defined YSE_DEBUG
  //INTERNAL::Global().getLog().emit(E_SOUND_ADDED);
#endif
}

YSE::SOUND::implementationObject::~implementationObject() {
	if (parent != nullptr) {
    parent->disconnect(this);
	}
  // only streams should delete their source. Other files are shared.
  if (file != nullptr && !streaming) {
    file->release(this);
  }
  if (post_dsp && post_dsp->calledfrom) post_dsp->calledfrom = nullptr;
  if (head.load() != nullptr) {
    head.load()->pimpl = nullptr;
  }

  if (patcher != nullptr && patcher->controlledBySound) {
    if (patcher->head != nullptr) {
      patcher->controlledBySound = false;
    }
    else {
      delete patcher;
    }
  }
}

bool YSE::SOUND::implementationObject::create(const std::string &fileName, channel * ch, Bool loop, Flt volume, Bool streaming) {
  parent = ch->pimpl;
  looping = loop;
  fader.set(volume);
  playerType = PT_FILE;
  this->streaming = streaming;

  std::string fullName;
  if (!IO().getActive()) {
    if (IsPathAbsolute(fileName)) {
      fullName = fileName;
    }
    else {
      fullName = GetCurrentWorkingDirectory() + "\\" + fileName;
    }
    
    if (!FileExists(fullName)) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + fullName);
      goto release;
    }
  }
  else {
    fullName = fileName;
    if (!INTERNAL::CALLBACK::fileExists(fileName.c_str())) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + fileName);
      goto release;
    }
  }

  if (!streaming) {
    file = SOUND::Manager().addFile(fullName);
    status_dsp = SS_STOPPED;
    status_upd = SS_STOPPED;

    if (file == nullptr) {
      goto release;
    } else {
      file->attach(this);
      objectStatus = OBJECT_CREATED;
      return true;
    }
  } else {
    // streams have their own soundfile
    streaming = true;
    status_dsp = SS_STOPPED;
    status_upd = SS_STOPPED;
      
    file = new INTERNAL::soundFile(fullName);
    
    if(file->create(true)) {
      filebuffer.resize(file->channels());
      buffer = &filebuffer;
      return true;
    } else {
      delete file;
      file = nullptr;
      goto release;
    }
  }

release:
  head = nullptr;
  return false;
}

Bool YSE::SOUND::implementationObject::create(YSE::DSP::buffer & buffer, channel * ch, Bool loop, Flt volume) {
  parent = ch->pimpl;
  looping = loop;
  fader.set(volume);
  playerType = PT_FILE;

  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  file = SOUND::Manager().addFile(&buffer);
  file->attach(this);
  if (file->create(false)) {
    filebuffer.resize(file->channels());
    this->buffer = &filebuffer;
    setup();
    return true;
  }
  else {
    delete file;
    file = nullptr;
    head = nullptr;
    return false;
  }
}

Bool YSE::SOUND::implementationObject::create(MULTICHANNELBUFFER & buffer, channel * ch, Bool loop, Flt volume) {
  parent = ch->pimpl;
  looping = loop;
  fader.set(volume);
  playerType = PT_FILE;

  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  file = SOUND::Manager().addFile(&buffer);
  file->attach(this);
  if (file->create(false)) {
    filebuffer.resize(file->channels());
    this->buffer = &filebuffer;
    setup();
    return true;
  }
  else {
    delete file;
    file = nullptr;
    head = nullptr;
    return false;
  }
}

bool YSE::SOUND::implementationObject::create(DSP::dspSourceObject & ptr, channel * ch, Flt volume) {
  parent = ch->pimpl;
  looping = false;
  fader.set(volume);
  playerType = PT_DSP;

  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  source_dsp = &ptr;
  buffer = &source_dsp->samples;
  return true;
}

bool YSE::SOUND::implementationObject::create(PATCHER::patcherImplementation * ptr, channel * ch, float volume) {
  parent = ch->pimpl;
  looping = false;
  fader.set(volume);
  playerType = PT_PATCHER;

  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  patcher = ptr;
  patcher->controlledBySound = true;
  buffer = &patcher->output;
  return true;
}
/*bool YSE::SOUND::implementationObject::create(SYNTH::implementationObject * ptr, channel * ch, Flt volume) {
  parent = ch->pimpl;
  looping = false;
  fader.set(volume);
  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  synth = ptr;
  buffer = &ptr->samples;
  return true;
}*/

void YSE::SOUND::implementationObject::setup() {
  if (objectStatus == OBJECT_DELETE) return;
  
  if (objectStatus >= OBJECT_CREATED) {
    // interface might be deleted
    if (head.load() == nullptr) {
      objectStatus = OBJECT_DELETE;
      return;
    }
    // if object is ready and head is not null, just return
    if (objectStatus == OBJECT_READY) return;

    if (source_dsp != nullptr || patcher != nullptr){// || synth != nullptr) {
      // dsp source sounds are a special case because there's no file involved
      resize();
    }
    else if (streaming) {
      // streaming sounds do not have to wait until loaded
      filebuffer.resize(file->channels());
	  _head_length = file->length();
      resize();
      
    } else if (file->getState() == INTERNAL::FILESTATE::READY) {
      // file is ready!
      filebuffer.resize(file->channels());
      buffer = &filebuffer;
	  _head_length = file->length();
      resize();
    }
    else if (file->getState() == INTERNAL::FILESTATE::INVALID) {
      objectStatus = OBJECT_DELETE;
      return;
    }
  }
  objectStatus = OBJECT_SETUP;
}

void YSE::SOUND::implementationObject::resize() {
  lastGain.resize(CHANNEL::Manager().getNumberOfOutputs());
  for (UInt i = 0; i < lastGain.size(); i++) {
    lastGain[i].resize(buffer->size(), 0.0f);
  }
}

Bool YSE::SOUND::implementationObject::readyCheck() {
  if (objectStatus == OBJECT_READY) {
    // this means we have done this check before and returned true back then.
    // the object is added to the list of inUse, but is probably not deleted just
    // yet. It will be deleted the next time the remove_if function runs (in objectManager)
    return false;
  }
  if (objectStatus == OBJECT_SETUP) {
    if (lastGain.size() == CHANNEL::Manager().getNumberOfOutputs()) {
      objectStatus = OBJECT_READY;
      return true;
    }
  }
  // if we get here, something was wrong. Re-run setup.
  objectStatus = OBJECT_CREATED;
  return false;
}

void YSE::SOUND::implementationObject::doThisWhenReady() {
  parent->connect(this);
}

void YSE::SOUND::implementationObject::sendMessage(const messageObject & message) {
  messages.push(message);
}

void YSE::SOUND::implementationObject::sync() {
  if (head.load() == nullptr) {
    objectStatus = OBJECT_DONE;
    
    // sound head is destructed, so stop and remove
    if (playerType == PT_DSP || playerType == PT_PATCHER) {
      objectStatus = OBJECT_RELEASE;
      return;
    }
    
    if (status_dsp != SS_STOPPED && status_dsp != SS_WANTSTOSTOP) {
      status_dsp = SS_WANTSTOSTOP;
    }
    else if (status_dsp == SS_STOPPED) {
      objectStatus = OBJECT_RELEASE;
    }
    return;
  }

  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }

  // sync dsp values
  currentVolume_upd = currentVolume_dsp;
  _head_time = currentFilePos;
  head.load()->_volume = currentVolume_dsp;
  status_upd = status_dsp;
  _head_status = status_upd;
}

void YSE::SOUND::implementationObject::parseMessage(const messageObject & message) {
  // get new values from head
  switch (message.ID) {
    case MESSAGE::POSITION: {
      pos.x = message.vecValue[0];
      pos.y = message.vecValue[1];
      pos.z = message.vecValue[2];
      break;
    }
    case MESSAGE::SPREAD: {
      spread = message.floatValue;
      break;
    }
    case MESSAGE::VOLUME_VALUE: {
      setVolume = true;
      volumeTime = 0.f; // assume zero, will be set by SM_VOLUME_TIME if needed
      volumeValue = message.floatValue;
      break;
    }
    case MESSAGE::VOLUME_TIME: {
      volumeTime = static_cast<Flt>(message.uintValue);
      break;
    }
    case MESSAGE::SPEED: {
      pitch = message.floatValue;
      break;
    }
    case MESSAGE::SIZE: {
      size = message.floatValue;
      break;
    }
    case MESSAGE::LOOP: {
      looping = message.boolValue;
      break;
    }
    case MESSAGE::INTENT: {
      headIntent = message.intentValue;
      break;
    }
    case MESSAGE::OCCLUSION: {
      occlusionActive = message.boolValue;
      break;
    }
    case MESSAGE::DSP: {
      addDSP(*(DSP::dspObject *)message.ptrValue);
      break;
    }
    case MESSAGE::TIME: {
      newFilePos = message.floatValue;
      setFilePos = true;
      break;
    }
    case MESSAGE::RELATIVE: {
      relative = message.boolValue;
      break;
    }
    case MESSAGE::DOPPLER: {
      doppler = message.boolValue;
      break;
    }
    case MESSAGE::PAN2D: {
      relative = message.boolValue;
      doppler = !message.boolValue;
      break;
    }
    case MESSAGE::FADE_AND_STOP: {
      fadeAndStopTime = static_cast<Flt>(message.uintValue);
      setFadeAndStop = true;
      break;
    }
    case MESSAGE::MOVE: {
      channel* ptr = (channel*)message.ptrValue;
      if (ptr != nullptr) {
        ptr->pimpl->connect(this);
      }
      break;
    }
  }
}



void YSE::SOUND::implementationObject::update() {
  ///////////////////////////////////////////
  // set position and distance
  ///////////////////////////////////////////
  newPos = pos * INTERNAL::Settings().distanceFactor;

  // distance to listener
  if (relative) {
    distance = Dist(Pos(0), newPos);
  }
  else {
    distance = Dist(newPos, INTERNAL::ListenerImpl().newPos);
  }
  virtualDist = (distance- size) * currentVolume_upd;
  if (virtualDist < 0) virtualDist = 0;
  /*if (virtualDist > 1000.f) {
    assert(false);
  }*/

  ///////////////////////////////////////////
  // calculate doppler effect 
  ///////////////////////////////////////////
  Flt vel = velocity; // avoid using atomic all the time
  if (!doppler) vel = 0;
  else {
    velocityVec = (newPos - lastPos) * (1 / INTERNAL::Time().delta());
    
    Pos listenerVelocity;
    listenerVelocity.x = INTERNAL::ListenerImpl().vel.x.load();
    listenerVelocity.y = INTERNAL::ListenerImpl().vel.y.load();
    listenerVelocity.z = INTERNAL::ListenerImpl().vel.z.load();

    if (velocityVec == Pos(0) && listenerVelocity == Pos(0)) vel = 0;
    else {
      Pos dist = relative ? newPos : newPos - INTERNAL::ListenerImpl().newPos;
      if (dist != Pos(0)) {
        Flt rSound = Dot(velocityVec, dist) / dist.length();
        Flt rList = Dot(listenerVelocity, dist) / dist.length();
        vel = 1 - (440 / (((344.0f + rList) / (344.0f + rSound)) * 440));
        vel *= INTERNAL::Settings().dopplerScale;
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
  Pos dir = relative ? newPos : newPos - INTERNAL::ListenerImpl().newPos;
  if (relative) a = -atan2(dir.x, dir.z);
  else a = (atan2(dir.x, dir.z) - atan2(INTERNAL::ListenerImpl().forward.x.load(), INTERNAL::ListenerImpl().forward.z.load()));
  while (a > Pi) a -= Pi2;
  while (a < -Pi) a += Pi2;
  angle = a; // back to atomic

  ///////////////////////////////////////////
  // sound occlusion (optional)
  ///////////////////////////////////////////
  if (System().occlusionCallback() != nullptr && occlusionActive) {
    occlusion_dsp = System().occlusionCallback()(newPos, INTERNAL::ListenerImpl().newPos);
    Clamp(occlusion_dsp, 0.f, 1.f);
  }

  ///////////////////////////////////////////
  // dsp processing (optional)
  ///////////////////////////////////////////
  if (_setPostDSP) {
    addDSP(*_postDspPtr);
    _setPostDSP = false;
  }
  
  ///////////////////////////////////////////
  // add to virtual sound calculator
  ///////////////////////////////////////////
  if (objectStatus < OBJECT_READY || status_upd == YSE::SS_STOPPED || status_upd == YSE::SS_PAUSED) {
    return;
  }
  VirtualSoundFinder().add(virtualDist);
}

Bool YSE::SOUND::implementationObject::dsp() {
  if (objectStatus == OBJECT_DELETE) return false;
  ///////////////////////////////////////////
  // handle play status
  ///////////////////////////////////////////
  dspFunc_parseIntent();

  if (status_dsp == SS_STOPPED || status_dsp == SS_PAUSED) return false;
  if (parent->allowVirtual && !VirtualSoundFinder().inRange(virtualDist)) return false;

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
  if (playerType == PT_DSP && source_dsp != nullptr) {
    source_dsp->process(status_dsp);
  }
  else if (playerType == PT_PATCHER && patcher != nullptr) {
    patcher->Calculate(YSE::T_DSP);
  }
  //else if (synth != nullptr) {
  //  synth->process(status_dsp);
  //} 
  else if (playerType == PT_FILE && file->read(filebuffer, filePtr, STANDARD_BUFFERSIZE, pitch + velocity, looping, status_dsp, bufferVolume) == false) {
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
  if (post_dsp != nullptr) {
    DSP::dspObject * ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer);
      ptr = ptr->link();
    }
  }
  return true;
}

void YSE::SOUND::implementationObject::dspFunc_parseIntent() {
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
        if (streaming) file->reset();
      }
      break;
    }

    case SI_TOGGLE:
    {
      if (status_dsp  == SS_PLAYING || status_dsp  == SS_WANTSTOPLAY || status_dsp  == SS_PLAYING_FULL_VOLUME) status_dsp  = SS_WANTSTOPAUSE;
      else status_dsp = SS_WANTSTOPLAY;
      break;
    }
      
  }

  headIntent = SI_NONE;
}


void YSE::SOUND::implementationObject::dspFunc_calculateGain(Int channel, Int source) {
  Flt finalGain = parent->outConf[channel].finalGain;
  if (lastGain[channel][source] == finalGain) {
    channelBuffer *= (finalGain);
    return;
  }

  Flt length = 50;
  Clamp(length, 1.f, static_cast<Flt>(channelBuffer.getLength()));
  Flt step = (finalGain - lastGain[channel][source]) / length;
  Flt multiplier = lastGain[channel][source];
  Flt * ptr = channelBuffer.getPtr();
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

void YSE::SOUND::implementationObject::toChannels() {
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
    Flt correctPower = 1 / pow(dist, (2 * INTERNAL::Settings().rolloffScale));
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

void YSE::SOUND::implementationObject::addDSP(DSP::dspObject & ptr) {
  if (post_dsp) {
    if (post_dsp->calledfrom) {
      *(post_dsp->calledfrom) = nullptr;
    }
  }

  post_dsp = &ptr;
  post_dsp->calledfrom = &post_dsp;
}

bool YSE::SOUND::implementationObject::sortSoundObjects(implementationObject * lhs, implementationObject * rhs) {
  if (!lhs->parent->allowVirtual) return true;
  if (!rhs->parent->allowVirtual) return false;
  return (lhs->virtualDist < rhs->virtualDist);
}

void YSE::SOUND::implementationObject::removeInterface() {
  head.store(nullptr);
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::SOUND::implementationObject::getStatus() {
  return objectStatus.load();
}

void YSE::SOUND::implementationObject::setStatus(YSE::OBJECT_IMPLEMENTATION_STATE value) {
  objectStatus.store(value);
}
