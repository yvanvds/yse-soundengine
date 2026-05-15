/*
  ==============================================================================

    soundLoader.cpp
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#include "soundManager.h"
#include "../internalHeaders.h"
#include "../internal/virtualFinder.h"
#include "../channel/channelManager.h"

YSE::SOUND::managerObject & YSE::SOUND::Manager() {
  static managerObject m;
  return m;
}


YSE::SOUND::managerObject::managerObject() 
  : mgrSetup(this),
    mgrDelete(this) {
  //formatManager.registerBasicFormats();
}

YSE::SOUND::managerObject::~managerObject() {

  // wait for jobs to finish
  mgrSetup.join();
  mgrDelete.join();

  // drain any pointers still queued by the main thread; they reference impls
  // owned by `implementations` and will be freed when that list is cleared.
  implementationObject * drained;
  while (toLoadInbox.try_pop(drained)) { (void)drained; }

  // remove all objects that are still in memory
  toLoad.clear();
  inUse.clear();
  implementations.clear();

  // remove all sounds that are still in memory
  soundFiles.clear();
}


YSE::INTERNAL::soundFile * YSE::SOUND::managerObject::addFile(const std::string & fileName) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->contains(fileName)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(fileName);
  INTERNAL::soundFile & sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  }
  else {
    return nullptr;
  }
}


YSE::INTERNAL::soundFile * YSE::SOUND::managerObject::addFile(YSE::DSP::buffer * buffer) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->contains(buffer)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(buffer);
  INTERNAL::soundFile & sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  }
  else {
    return nullptr;
  }
}

YSE::INTERNAL::soundFile * YSE::SOUND::managerObject::addFile(MULTICHANNELBUFFER * buffer) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->contains(buffer)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(buffer);
  INTERNAL::soundFile & sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  }
  else {
    return nullptr;
  }
}

YSE::SOUND::implementationObject * YSE::SOUND::managerObject::addImplementation(YSE::sound * head) {
  std::lock_guard<std::mutex> lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::SOUND::managerObject::setup(YSE::SOUND::implementationObject * impl) {
  impl->setStatus(OBJECT_CREATED);
  // Hand off to the audio thread via the lock-free inbox; the audio thread
  // will drain it into `toLoad` at the top of update().
  toLoadInbox.push(impl);
}

void YSE::SOUND::managerObject::update() {
  ///////////////////////////////////////////
  // drain the main→audio inbox of newly-set-up impls
  ///////////////////////////////////////////
  {
    implementationObject * p;
    while (toLoadInbox.try_pop(p)) toLoad.emplace_front(p);
  }

  ///////////////////////////////////////////
  // update actual soundfiles
  ///////////////////////////////////////////
  auto iMinus = soundFiles.before_begin();
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ) {
    
    // don't handle files currently in the thread pool
    if (i->isQueued()) {
      iMinus = i;
      ++i;
      continue;
    }

    // invalid files or files no longer in use should be removed from memory
    if (!i->inUse()) {
      i = soundFiles.erase_after(iMinus);
    }
    else {
      iMinus = i;
      ++i;
    }
  }

  VirtualSoundFinder().reset();
  
  ///////////////////////////////////////////
  // check if there are implementations that need setup
  ///////////////////////////////////////////
  if (!toLoad.empty() && !mgrSetup.isQueued()) {
    // Custom remove pass: erases READY / RELEASE / DELETE impls (already
    // handled or in flight) and *handshakes* OBJECT_DELETE_PENDING impls
    // (setup-failure path) by promoting them to OBJECT_DELETE and triggering
    // the slow-pool delete job — but only AFTER erasing them from toLoad,
    // so the slow-pool can't free a pointer that's still in the
    // audio-thread-iterated list. See enums.hpp for the PENDING rationale.
    auto previous = toLoad.before_begin();
    for (auto it = toLoad.begin(); it != toLoad.end(); ) {
      implementationObject * p = *it;
      OBJECT_IMPLEMENTATION_STATE s = p->objectStatus.load(std::memory_order_acquire);
      if (s == OBJECT_DELETE_PENDING) {
        p->objectStatus.store(OBJECT_DELETE, std::memory_order_release);
        runDelete = true;
        it = toLoad.erase_after(previous);
      } else if (s == OBJECT_READY || s == OBJECT_RELEASE || s == OBJECT_DELETE) {
        it = toLoad.erase_after(previous);
      } else {
        previous = it;
        ++it;
      }
    }
    INTERNAL::Global().addSlowJob(&mgrSetup);
  }

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loaded implementations are ready
  //
  // When readyCheck succeeds we move the impl into inUse AND erase it from
  // toLoad in the same step. Deferring the toLoad-erasure to the next tick's
  // remove_if creates a use-after-free window: within this same update tick
  // the impl can subsequently transition through OBJECT_RELEASE→OBJECT_DELETE
  // in the inUse iteration below, runDelete is set, deleteJob is enqueued,
  // the slow-pool frees the impl, and the next remove_if call dereferences
  // the freed pointer (ASan-confirmed).
  ///////////////////////////////////////////
  {
    auto previous = toLoad.before_begin();
    for (auto i = toLoad.begin(); i != toLoad.end(); ) {
      implementationObject * ptr = *i;
      if (ptr->readyCheck()) {
        inUse.emplace_front(ptr);
        // add the sound to the channel that is supposed to use
        //ptr->parent->connect(ptr);
        ptr->doThisWhenReady();
        i = toLoad.erase_after(previous);
      } else {
        previous = i;
        ++i;
      }
    }
  }

  ///////////////////////////////////////////
  // sync and update implementations
  ///////////////////////////////////////////
  {
    auto previous = inUse.before_begin();
    for (auto i = inUse.begin(); i != inUse.end();) {
      (*i)->sync();
      if ((*i)->getStatus() == OBJECT_RELEASE) {
        implementationObject * ptr = (*i);
        i = inUse.erase_after(previous);
        // Audio-thread-side disconnect: remove this impl from parent->sounds
        // BEFORE marking it OBJECT_DELETE. The slow-pool's deleteJob filters
        // on OBJECT_DELETE, so any impl visible to it has already been pulled
        // from the audio-thread-iterated `sounds` list — no race on
        // sounds.remove() in the destructor.
        if (ptr->parent != nullptr && ptr->connectedToParent.load(std::memory_order_acquire)) {
          ptr->parent->disconnect(ptr);
          ptr->connectedToParent.store(false, std::memory_order_release);
        }
        // Defensive: null the user-supplied DSP source pointer before the
        // impl becomes eligible for destruction. If the user destroyed their
        // dspSourceObject slightly before this point, the audio thread's
        // dsp() will now load nullptr instead of a dangling pointer.
        ptr->source_dsp.store(nullptr, std::memory_order_release);
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue;
      }
      // update
      (*i)->update();
      previous = i;
      ++i;
    }
  }

  VirtualSoundFinder().calculate();
  
}

Bool YSE::SOUND::managerObject::empty() {
  return implementations.empty();
}

/*AudioFormatReader * YSE::SOUND::managerObject::getReader(const File & f) {
  return formatManager.createReaderFor(f);
}

AudioFormatReader * YSE::SOUND::managerObject::getReader(juce::InputStream * source) {
  return formatManager.createReaderFor(source);
}*/



void YSE::SOUND::managerObject::adjustLastGainBuffer() {
  for (auto i = inUse.begin(); i != inUse.end(); ++i) {
    UInt j = static_cast<UInt>((*i)->lastGain.size()); // need to store previous size for deep resize
    (*i)->lastGain.resize(CHANNEL::Manager().getNumberOfOutputs());
    for (; j < (*i)->lastGain.size(); j++) {
      (*i)->lastGain[j].resize((*i)->buffer->size(), 0.0f);
    }
  }
}
