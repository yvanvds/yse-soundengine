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

namespace {
  // Time constant (ms) over which the doppler playback-rate ratio is slewed in
  // dsp(). Long enough to iron out the stepwise, jittery update-tick target
  // into an audible glide, short enough that the pitch still tracks a fast
  // mover without lag. See issue #208.
  constexpr Int DOPPLER_SLEW_MS = 30;
} // namespace

YSE::SOUND::implementationObject::implementationObject(sound* head)
  : _head_streaming(false),
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
    dopplerRatio(1.f),
    pitch(1.f),
    size(1.0f),
    isVirtual(false),
    virtualFadeOut(false),
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
    objectStatus(OBJECT_CONSTRUCTED) {
  fader.set(0.5f);

#if defined YSE_DEBUG
  // INTERNAL::Global().getLog().emit(E_SOUND_ADDED);
#endif
}

YSE::SOUND::implementationObject::~implementationObject() {
  // The primary disconnect path is on the audio thread, in
  // SOUND::Manager::update at the OBJECT_RELEASE→OBJECT_DELETE transition.
  // This guard ensures the slow-pool's destructor only touches
  // parent->sounds when no audio-thread disconnect has happened yet (i.e.
  // setup-failure path: impls that died before doThisWhenReady ever ran).
  // In that case `connectedToParent` is false and parent->sounds doesn't
  // contain us — the slow-pool just skips the disconnect entirely.
  if (parent != nullptr &&
      connectedToParent.load(
          std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with release
                                        // in doThisWhenReady() / Manager::update() handshake
    parent->disconnect(this);
  }
  if (file != nullptr) {
    if (streaming) {
      // Streaming impls own their soundFile outright — it is allocated with
      // `new` in create() and never registered with the shared-file manager,
      // so nothing else will ever free it. Delete it exactly once here (issue
      // #218). This destructor runs on the slow-pool deleteJob
      // (managerDeleteJob::run erasing from the implementations list), never
      // the audio thread — which is the only place blocking is allowed,
      // because ~soundFile join()s an in-flight back-buffer refill before
      // freeing its handle/buffers (constraint from #185).
      delete file;
      file = nullptr;
    } else {
      // Other files are shared and reference-counted by the manager; just drop
      // our client reference so the GC can reclaim it once idle.
      file->release(this);
    }
  }
  if (post_dsp && post_dsp->calledfrom) post_dsp->calledfrom = nullptr;
  if (sound* h = head.load(
          std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — snapshot head,
                                        // paired with release stores from removeInterface()
    h->pimpl = nullptr;
  }

  if (patcher != nullptr && patcher->controlledBySound) {
    if (patcher->head != nullptr) {
      patcher->controlledBySound = false;
    } else {
      delete patcher;
    }
  }
}

#ifdef __WINDOWS_
std::string delim = "\\";
#else
std::string delim = "/";
#endif

///
bool YSE::SOUND::implementationObject::create(const std::string& fileName, channel* ch, Bool loop,
                                              Flt volume, Bool streaming) {
  parent = ch->pimpl;
  looping = loop;
  fader.set(volume);
  playerType = PT_FILE;
  this->streaming = streaming;

  std::string fullName;
  if (!IO().getActive()) {
    if (IsPathAbsolute(fileName)) {
      fullName = fileName;
    } else {
      fullName = GetCurrentWorkingDirectory() + delim + fileName;
    }

    if (!FileExists(fullName)) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + fullName);
      goto release;
    }
  } else {
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

    if (file->create(true)) {
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

///
Bool YSE::SOUND::implementationObject::create(YSE::DSP::buffer& buffer, channel* ch, Bool loop,
                                              Flt volume) {
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
  } else {
    delete file;
    file = nullptr;
    head = nullptr;
    return false;
  }
}

Bool YSE::SOUND::implementationObject::create(MULTICHANNELBUFFER& buffer, channel* ch, Bool loop,
                                              Flt volume) {
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
  } else {
    delete file;
    file = nullptr;
    head = nullptr;
    return false;
  }
}

bool YSE::SOUND::implementationObject::create(DSP::dspSourceObject& ptr, channel* ch, Flt volume) {
  parent = ch->pimpl;
  looping = false;
  fader.set(volume);
  playerType = PT_DSP;

  status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  source_dsp.store(
      &ptr, std::memory_order_release); // NOSONAR S8417: intentional release — publishes dsp source
                                        // pointer to audio thread's acquire load in dsp()
  buffer = &ptr.samples;
  return true;
}

bool YSE::SOUND::implementationObject::create(PATCHER::patcherImplementation* ptr, channel* ch,
                                              float volume) {
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
/*bool YSE::SOUND::implementationObject::create(SYNTH::implementationObject * ptr, channel * ch, Flt
volume) { parent = ch->pimpl; looping = false; fader.set(volume); status_dsp = SS_STOPPED;
  status_upd = SS_STOPPED;

  synth = ptr;
  buffer = &ptr->samples;
  return true;
}*/

void YSE::SOUND::implementationObject::setup() {
  if (objectStatus == OBJECT_DELETE || objectStatus == OBJECT_DELETE_PENDING) return;

  if (objectStatus >= OBJECT_CREATED) {
    // interface might be deleted
    if (head.load() == nullptr) {
      // OBJECT_DELETE_PENDING (not OBJECT_DELETE) so the audio thread can
      // remove this impl from toLoad before the slow-pool's deleteJob is
      // allowed to free it. The audio thread promotes PENDING -> DELETE
      // after erasing from toLoad in soundManager::update().
      objectStatus = OBJECT_DELETE_PENDING;
      return;
    }
    // if object is ready and head is not null, just return
    if (objectStatus == OBJECT_READY) return;

    if (source_dsp.load(std::memory_order_acquire) != nullptr ||
        patcher !=
            nullptr) { // || synth != nullptr) { // NOSONAR S8417: intentional acquire — pairs with
                       // release in create() to safely observe published dsp source
      // dsp source sounds are a special case because there's no file involved
      resize();
    } else if (streaming) {
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
    } else if (file->getState() == INTERNAL::FILESTATE::INVALID) {
      // Same toLoad-lifetime reasoning as the head==null branch above.
      objectStatus = OBJECT_DELETE_PENDING;
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
  // Audio thread now owns the link into parent->sounds. Mark it so that the
  // audio thread's release path (in SOUND::Manager::update) knows it must
  // disconnect us before allowing the slow-pool deleteJob to free us.
  connectedToParent.store(
      true,
      std::memory_order_release); // NOSONAR S8417: intentional release — publishes audio-thread
                                  // connect to the dtor / Manager::update() acquire loads
}

void YSE::SOUND::implementationObject::sendMessage(const messageObject& message) {
#ifndef NDEBUG
  // Single-producer contract (issue #193): `messages` is an SPSC queue, so
  // every push must originate on the same control thread. The engine keeps
  // this true by deferring off-thread bus publishes (the gMetro timer thread,
  // a script thread) to the control thread in NamedBus::drainPending(). A
  // firing assert means an interface setter was driven concurrently from a
  // second thread — fix that caller rather than widening the queue.
  const std::thread::id caller = std::this_thread::get_id();
  if (!_producerThreadKnown) {
    _producerThread = caller;
    _producerThreadKnown = true;
  } else {
    assert(caller == _producerThread &&
           "sound message queue pushed from more than one thread (issue #193)");
  }
#endif
  messages.push(message);
}

void YSE::SOUND::implementationObject::sync() {
  // Snapshot head once so we can detect that ~sound() has run: removeInterface()
  // nulls `head` (release store) when the user object is destroyed, and this
  // acquire load observes that. We use the snapshot ONLY to branch on
  // destruction — sync() must never write through it. ~sound() can complete
  // (and the user-owned interface storage be reclaimed) at any point after the
  // load, so a store back through `h` would be a use-after-free (issue #191).
  // All values the interface reads back are published through impl-side atomics
  // (`_head_time`, `_head_status`, ...) that outlive the interface, never pushed
  // into the interface object.
  sound* h =
      head.load(std::memory_order_acquire); // NOSONAR S8417: intentional acquire — snapshot head
                                            // once to avoid race with ~sound()/removeInterface()
  if (h == nullptr) {
    objectStatus = OBJECT_DONE;

    // sound head is destructed, so stop and remove
    if (playerType == PT_DSP || playerType == PT_PATCHER) {
      objectStatus = OBJECT_RELEASE;
      return;
    }

    if (status_dsp != SS_STOPPED && status_dsp != SS_WANTSTOSTOP) {
      status_dsp = SS_WANTSTOSTOP;
    } else if (status_dsp == SS_STOPPED) {
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
  // NB: the actual playing volume is intentionally NOT pushed back into the
  // user-owned interface object here. Doing so (`h->_volume = ...`) raced
  // ~sound() and wrote freed memory (issue #191). `sound::volume()` returns
  // the last-set (clamped) value from its own cache, consistent with every
  // sibling getter (speed/spread/size/pos), none of which sync() writes to.
  status_upd = status_dsp;
  _head_status = status_upd;
}

void YSE::SOUND::implementationObject::parseMessage(const messageObject& message) {
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
  case MESSAGE::OCCLUSION_VALUE: {
    // Result of the user occlusion callback, computed on the control thread in
    // SOUND::updateOcclusion() and already clamped to [0, 1] there (issue #209).
    occlusion_dsp = message.floatValue;
    break;
  }
  case MESSAGE::DSP: {
    addDSP(*(DSP::dspObject*)message.ptrValue);
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
  } else {
    distance = Dist(newPos, INTERNAL::ListenerImpl().newPos);
  }
  virtualDist = computeVirtualDist(distance, size, currentVolume_upd);

  ///////////////////////////////////////////
  // calculate doppler effect
  ///////////////////////////////////////////
  // Doppler is a frequency *ratio* applied multiplicatively to the playback
  // rate (see computeDopplerRatio / issue #208). update() computes the stepwise
  // target here; dsp() slews it at block rate so an uneven update tick can't
  // warble the pitch. A ratio of 1.0 means "no shift".
  Flt ratio = 1.0f;
  if (doppler) {
    velocityVec = (newPos - lastPos) * (1 / INTERNAL::Time().delta());

    Pos listenerVelocity = INTERNAL::ListenerImpl().vel.load();

    if (!(velocityVec == Pos(0) && listenerVelocity == Pos(0))) {
      Pos dist = relative ? newPos : newPos - INTERNAL::ListenerImpl().newPos;
      ratio = computeDopplerRatio(velocityVec, listenerVelocity, dist,
                                  INTERNAL::Settings().dopplerScale);
    }
  }
  lastPos = newPos;
  dopplerRatio = ratio; // back to atomic; slewed in dsp()

  ///////////////////////////////////////////
  // calculate angle
  ///////////////////////////////////////////
  Flt a = angle; // avoid using atomic all the time
  Pos dir = relative ? newPos : newPos - INTERNAL::ListenerImpl().newPos;
  if (relative)
    a = -atan2(dir.x, dir.z);
  else {
    const Pos listenerForward = INTERNAL::ListenerImpl().forward.load();
    a = (atan2(dir.x, dir.z) - atan2(listenerForward.x, listenerForward.z));
  }
  while (a > Pi)
    a -= Pi2;
  while (a < -Pi)
    a += Pi2;
  angle = a; // back to atomic

  ///////////////////////////////////////////
  // sound occlusion (optional)
  ///////////////////////////////////////////
  // The user occlusion callback is NOT invoked here: update() runs on the audio
  // callback thread (deviceManager::doOnCallback -> SOUND::Manager().update()),
  // and user raycast code must never run there (issue #209). The callback now
  // runs on the control thread in SOUND::updateOcclusion() (driven by
  // System().update()); its clamped result arrives via the OCCLUSION_VALUE
  // message and is stored in occlusion_dsp, which dsp() applies as a gain duck.

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
  if (objectStatus < OBJECT_READY || status_upd == YSE::SS_STOPPED ||
      status_upd == YSE::SS_PAUSED) {
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

  if (playerType == PT_PATCHER && status_dsp != SS_PLAYING) {
    switch (status_dsp) {
    case SS_STOPPED:
    case SS_PAUSED:
    case SS_PLAYING:
    case SS_PLAYING_FULL_VOLUME:
      break;
    case SS_WANTSTOPLAY:
      status_dsp = SS_PLAYING;
      break;
    case SS_WANTSTORESTART:
      status_dsp = SS_PLAYING;
      break;
    case SS_WANTSTOPAUSE:
      status_dsp = SS_PAUSED;
      break;
    case SS_WANTSTOSTOP:
      status_dsp = SS_STOPPED;
      break;
    }
  }

  if (status_dsp == SS_STOPPED || status_dsp == SS_PAUSED) return false;

  ///////////////////////////////////////////
  // virtualization (issue #206)
  ///////////////////////////////////////////
  // A sound crossing the virtualization threshold must not be hard-muted
  // between two blocks (that steps its contribution to zero in one buffer — an
  // audible click). Instead it gets exactly one farewell block whose channel
  // gains are forced to 0, so the 50-sample ramp in dspFunc_calculateGain()
  // slides it down to silence. inRange() applies a hysteresis band around the
  // cutoff so a sound sitting on the boundary doesn't flutter real/virtual.
  if (parent->allowVirtual) {
    bool real = VirtualSoundFinder().inRange(virtualDist, /*wasReal=*/!isVirtual);
    virtualAction act = computeVirtualAction(real, isVirtual);
    isVirtual = act.nowVirtual;
    virtualFadeOut = act.fadeOut;
    if (!act.render) return false;
  } else {
    isVirtual = false;
    virtualFadeOut = false;
  }

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
    if (streaming) {
      // For a streaming sound filePtr is buffer-local (issue #185), so an
      // absolute frame index can't just be assigned to it. Arm an async seek:
      // the disk seek + re-prime from `newFilePos` runs on the slow pool (never
      // the audio thread), and filePtr resets to the start of the freshly primed
      // front buffer. _frontBufferBase is set to the target inside seek(). (#217)
      file->seek(static_cast<Long>(newFilePos), looping);
      filePtr = 0.f;
    } else {
      // Non-streaming: the whole file is resident, so filePtr is a valid
      // absolute index into the buffer.
      filePtr = newFilePos;
    }
    setFilePos = false;
  }

  ///////////////////////////////////////////
  // fill buffer
  ///////////////////////////////////////////
  // Slew the doppler ratio toward its latest (stepwise) target at block rate so
  // the playback rate glides instead of stepping. Playback rate is the ratio
  // times the user pitch — doppler is multiplicative, never additive (#208).
  dopplerSlew.setIfNew(dopplerRatio, DOPPLER_SLEW_MS);
  dopplerSlew.update();
  Flt playbackRate = pitch * dopplerSlew();

  DSP::dspSourceObject* src = source_dsp.load(
      std::memory_order_acquire); // NOSONAR S8417: intentional acquire — pairs with release in
                                  // create()/Manager::update() to read user-supplied dsp source
  if (playerType == PT_DSP && src != nullptr) {
    src->process(status_dsp);
  } else if (playerType == PT_PATCHER && patcher != nullptr) {

    patcher->Calculate(YSE::T_DSP);
  }
  // else if (synth != nullptr) {
  //   synth->process(status_dsp);
  // }
  else if (playerType == PT_FILE &&
           file->read(filebuffer, filePtr, STANDARD_BUFFERSIZE, playbackRate, looping, status_dsp,
                      bufferVolume) == false) {
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
    DSP::dspObject* ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer);
      ptr = ptr->link();
    }
  }
  return true;
}

void YSE::SOUND::implementationObject::dspFunc_parseIntent() {
  switch (headIntent) {
  case SI_RESTART: {
    status_dsp = SS_WANTSTORESTART;
    break;
  }

  case SI_PLAY: {
    if (status_dsp != SS_PLAYING && status_dsp != SS_PLAYING_FULL_VOLUME) {
      status_dsp = SS_WANTSTOPLAY;
    }
    break;
  }

  case SI_PAUSE: {
    if (status_dsp != SS_STOPPED && status_dsp != SS_PAUSED) {
      status_dsp = SS_WANTSTOPAUSE;
    }
    break;
  }

  case SI_STOP: {
    if (status_dsp != SS_STOPPED && status_dsp != SS_PAUSED) {
      status_dsp = SS_WANTSTOSTOP;
    } else if (status_dsp == SS_PAUSED) {
      status_dsp = SS_STOPPED;
      filePtr = 0;
      if (streaming) file->reset();
    }
    break;
  }

  case SI_TOGGLE: {
    if (status_dsp == SS_PLAYING || status_dsp == SS_WANTSTOPLAY ||
        status_dsp == SS_PLAYING_FULL_VOLUME)
      status_dsp = SS_WANTSTOPAUSE;
    else
      status_dsp = SS_WANTSTOPLAY;
    break;
  }

  case SI_NONE:
    break;
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
  Flt* ptr = channelBuffer.getPtr();
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

  while (leftOvers--)
    *ptr++ *= finalGain;
  lastGain[channel][source] = finalGain;
}

void YSE::SOUND::implementationObject::toChannels() {
#ifdef _MSC_VER
#pragma warning(disable : 4258)
#endif
  for (UInt x = 0; x < buffer->size(); x++) {
    // calculate spread value for multichannel sounds
    Flt spreadAdjust = 0;
    if (buffer->size() > 1)
      spreadAdjust = (((2 * Pi / buffer->size()) * x) + (Pi / buffer->size()) - Pi) * spread;

    // initial panning
    for (UInt i = 0; i < parent->outConf.size(); i++) {
      parent->outConf[i].initPan =
          (1 + cos(parent->outConf[i].angle - (angle + spreadAdjust))) * 0.5f;
      parent->outConf[i].effective = 0;
      // effective speakers
      for (UInt j = 0; j < parent->outConf.size(); j++) {
        parent->outConf[i].effective +=
            computeSpeakerOverlap(parent->outConf[i].angle, parent->outConf[j].angle);
      }
      // initial gain
      parent->outConf[i].initGain = parent->outConf[i].initPan / parent->outConf[i].effective;
    }
    // emitted power
    Flt power = 0;
    for (UInt i = 0; i < parent->outConf.size(); i++) {
      power += static_cast<Flt>(pow(parent->outConf[i].initGain, 2));
    }
    // calculated power
    Flt dist = distance - size;
    if (dist < 0) dist = 0;
    Flt correctPower = 1 / pow(dist, (2 * INTERNAL::Settings().rolloffScale));
    if (correctPower > 1) correctPower = 1;

    // final gain assignment
    for (UInt j = 0; j < parent->out.size(); ++j) {
      parent->outConf[j].ratio = computePanRatio(parent->outConf[j].initGain, power,
                                                 static_cast<UInt>(parent->out.size()));
      channelBuffer = (*buffer)[x];
      parent->outConf[j].finalGain = sqrt(correctPower * parent->outConf[j].ratio);

      // add volume control now
      if (occlusionActive) parent->outConf[j].finalGain *= 1 - occlusion_dsp;
      // Farewell block for a sound going virtual (#206): target gain 0 so
      // dspFunc_calculateGain() ramps from lastGain down to silence instead of
      // the sound simply vanishing on the next (skipped) block.
      if (virtualFadeOut) parent->outConf[j].finalGain = 0.f;
      dspFunc_calculateGain(j, x);
      channelBuffer *= fader();
      parent->out[j] += channelBuffer;
    }
  }
}

void YSE::SOUND::implementationObject::addDSP(DSP::dspObject& ptr) {
  // Detach any previously-attached DSP from this impl. Clear the OLD
  // dspObject's back-reference (calledfrom) — otherwise, when the OLD
  // dspObject is destructed later (e.g. a process-lifetime static at
  // exit), its destructor's `*calledfrom = nullptr` would write to this
  // impl's post_dsp field, which may have been freed by deleteJob.
  // ASan-caught UAF: see Tests/sound/test_sound_impl.cpp "replacing a
  // DSP plugin clears the old calledfrom".
  if (post_dsp) {
    post_dsp->calledfrom = nullptr;
  }

  post_dsp = &ptr;
  post_dsp->calledfrom = &post_dsp;
}

Flt YSE::SOUND::implementationObject::computeVirtualDist(Flt distance, Flt size, Flt volume) {
  // Effective distance beyond the sound's own size; a near/large sound whose
  // listener sits inside `size` clamps to 0 (maximally important).
  Flt effectiveDist = distance - size;
  if (effectiveDist < 0) effectiveDist = 0;

  // Importance rises with volume and falls with distance: divide the clamped
  // effective distance by volume so quiet sounds score high (virtualized first)
  // and loud near sounds score low (kept real). A muted sound (volume ~ 0)
  // yields a very large value and is virtualized outright. (#205)
  constexpr Flt minVolume = 0.0001f;
  if (volume < minVolume) volume = minVolume;
  return effectiveDist / volume;
}

Flt YSE::SOUND::implementationObject::computeSpeakerOverlap(Flt angleA, Flt angleB) {
  // Cardioid weight in [0..1] — the same parenthesization as the pan term above
  // so a "how much do these two speakers overlap" weight and a "how much does
  // this speaker face the source" pan use one consistent curve. Coincident
  // speakers overlap fully (1), opposite speakers not at all (0). (#207)
  return (1 + cos(angleA - angleB)) * 0.5f;
}

Flt YSE::SOUND::implementationObject::computePanRatio(Flt initGain, Flt power, UInt speakerCount) {
  // Guard the pan normalisation against a zero (or non-finite) total power.
  // `power` is the sum of pow(initGain,2) over all speakers; it collapses to ~0
  // when every speaker is antipodal to the source angle (a mono layout with the
  // source directly behind the listener, or any degenerate custom layout).
  // pow(initGain,2)/power is then 0/0 = NaN, which poisons the whole mix and
  // latches into lastGain (#202). `!(power > minPower)` also rejects NaN power.
  // Fall back to an equal 1/N split so every speaker gets a finite share.
  constexpr Flt minPower = 1e-9f;
  if (speakerCount == 0) return 0.f;
  if (!(power > minPower)) return 1.f / static_cast<Flt>(speakerCount);
  return static_cast<Flt>(pow(initGain, 2) / power);
}

Flt YSE::SOUND::implementationObject::computeDopplerRatio(const Pos& sourceVel,
                                                          const Pos& listenerVel, const Pos& dist,
                                                          Flt dopplerScale) {
  constexpr Flt speedOfSound = 344.0f; // m/s, dry air ~20°C
  // Two octaves either way. A super-sonic closing speed would otherwise drive
  // the denominator toward (or past) zero and blow the playback rate up or
  // negative; clamping keeps the audio thread safe for any input. (#208)
  constexpr Flt minRatio = 0.25f;
  constexpr Flt maxRatio = 4.0f;

  Pos d = dist; // local copy: Pos::length() is non-const
  Flt len = d.length();
  if (len == 0.f) return 1.0f; // co-located: no well-defined radial axis, no shift

  // Radial (along the source→listener axis) components of each velocity.
  Flt rSource = Dot(sourceVel, d) / len;
  Flt rList = Dot(listenerVel, d) / len;

  // Observed = emitted * (c + rList) / (c + rSource). Applied multiplicatively
  // to the playback rate, unlike the old additive `1 - 1/ratio` linearisation
  // which was only accurate near pitch = 1 and low speeds.
  Flt denom = speedOfSound + rSource;
  if (denom <= 0.f) return maxRatio; // source closing at/above the speed of sound
  Flt ratio = (speedOfSound + rList) / denom;

  // dopplerScale amplifies/attenuates the deviation from unity, not the ratio
  // itself, so scale = 0 disables the effect and scale = 1 is physical.
  ratio = 1.0f + (ratio - 1.0f) * dopplerScale;

  Clamp(ratio, minRatio, maxRatio);
  return ratio;
}

YSE::SOUND::implementationObject::virtualAction
YSE::SOUND::implementationObject::computeVirtualAction(bool real, bool wasVirtual) {
  // Real: render normally (re-entry from virtual ramps back up from the 0 that
  // the farewell block left in lastGain, so no stale-gain jump either).
  if (real) return {/*render=*/true, /*fadeOut=*/false, /*nowVirtual=*/false};
  // First block going virtual: still render, but with gains forced to 0 so the
  // block ramps down to silence (one farewell fade instead of a click).
  if (!wasVirtual) return {/*render=*/true, /*fadeOut=*/true, /*nowVirtual=*/true};
  // Already faded out on a previous block: stay silent (skip render entirely).
  return {/*render=*/false, /*fadeOut=*/false, /*nowVirtual=*/true};
}

bool YSE::SOUND::implementationObject::sortSoundObjects(implementationObject* lhs,
                                                        implementationObject* rhs) {
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
