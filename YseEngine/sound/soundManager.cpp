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

YSE::SOUND::managerObject& YSE::SOUND::Manager() {
  static managerObject m;
  return m;
}

YSE::SOUND::managerObject::managerObject() : mgrSetup(this), mgrDelete(this) {
  // formatManager.registerBasicFormats();
  mgrFileGC.owner = this;
}

YSE::SOUND::managerObject::~managerObject() noexcept {
  try {
    // wait for jobs to finish
    mgrSetup.join();
    mgrDelete.join();
    mgrFileGC.join();

    // drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and will be freed when that list is cleared.
    implementationObject* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    // remove all objects that are still in memory
    toLoad.clear();
    inUse.clear();
    implementations.clear();

    // remove all sounds that are still in memory
    {
      std::scoped_lock lk(soundFilesMutex);
      soundFiles.clear();
    }
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "SOUND::Manager destructor swallowed exception");
  }
}

YSE::INTERNAL::soundFile* YSE::SOUND::managerObject::addFile(const std::string& fileName) {
  // Serialise with the slow-pool GC job that erases from soundFiles (issue #186).
  std::scoped_lock lk(soundFilesMutex);
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if (i->contains(fileName)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(fileName);
  INTERNAL::soundFile& sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  } else {
    return nullptr;
  }
}

YSE::INTERNAL::soundFile* YSE::SOUND::managerObject::addFile(YSE::DSP::buffer* buffer) {
  // Serialise with the slow-pool GC job that erases from soundFiles (issue #186).
  std::scoped_lock lk(soundFilesMutex);
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if (i->contains(buffer)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(buffer);
  INTERNAL::soundFile& sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  } else {
    return nullptr;
  }
}

YSE::INTERNAL::soundFile* YSE::SOUND::managerObject::addFile(MULTICHANNELBUFFER* buffer) {
  // Serialise with the slow-pool GC job that erases from soundFiles (issue #186).
  std::scoped_lock lk(soundFilesMutex);
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if (i->contains(buffer)) {
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(buffer);
  INTERNAL::soundFile& sf = soundFiles.front();
  if (sf.create()) {
    return &sf;
  } else {
    return nullptr;
  }
}

YSE::SOUND::implementationObject* YSE::SOUND::managerObject::addImplementation(YSE::sound* head) {
  std::scoped_lock lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::SOUND::managerObject::setup(YSE::SOUND::implementationObject* impl) {
  impl->setStatus(OBJECT_CREATED);
  // Hand off to the audio thread via the lock-free inbox; the audio thread
  // will drain it into `toLoad` at the top of update().
  toLoadInbox.push(impl);
}

void YSE::SOUND::managerObject::drainInbox() {
  implementationObject* p;
  while (toLoadInbox.try_pop(p))
    toLoad.push_front(p);
}

void YSE::SOUND::managerObject::scrubToLoadAndScheduleSetup() {
  // Custom remove pass: erases READY / RELEASE / DELETE impls (already
  // handled or in flight) and *handshakes* OBJECT_DELETE_PENDING impls
  // (setup-failure path) by promoting them to OBJECT_DELETE and triggering
  // the slow-pool delete job — but only AFTER erasing them from toLoad,
  // so the slow-pool can't free a pointer that's still in the
  // audio-thread-iterated list. See enums.hpp for the PENDING rationale.
  for (auto c = toLoad.front(); c.valid();) {
    implementationObject* p = c.get();
    OBJECT_IMPLEMENTATION_STATE s = p->objectStatus.load(
        std::memory_order_acquire); // NOSONAR S8417: intentional acquire — read impl state
                                    // published by setup-failure path
    if (s == OBJECT_DELETE_PENDING) {
      p->objectStatus.store(
          OBJECT_DELETE,
          std::memory_order_release); // NOSONAR S8417: intentional release — publish DELETE state
                                      // to slow-pool deleteJob's acquire load
      runDelete = true;
      c.erase();
    } else if (s == OBJECT_READY || s == OBJECT_RELEASE || s == OBJECT_DELETE) {
      c.erase();
    } else {
      c.next();
    }
  }
  INTERNAL::Global().addSlowJob(&mgrSetup);
}

void YSE::SOUND::managerObject::promoteReadyImpls() {
  // When readyCheck succeeds we move the impl into inUse AND erase it from
  // toLoad in the same step. Deferring the toLoad-erasure to the next tick's
  // remove_if creates a use-after-free window: within this same update tick
  // the impl can subsequently transition through OBJECT_RELEASE→OBJECT_DELETE
  // in the inUse iteration below, runDelete is set, deleteJob is enqueued,
  // the slow-pool frees the impl, and the next remove_if call dereferences
  // the freed pointer (ASan-confirmed).
  for (auto c = toLoad.front(); c.valid();) {
    implementationObject* ptr = c.get();
    if (ptr->readyCheck()) {
      // Unlink from toLoad BEFORE linking into inUse: both lists share the
      // impl's single `_mgrNext` link, so the erase (which reads _mgrNext to
      // stitch toLoad) must happen before push_front overwrites it (issue #194).
      c.erase();
      inUse.push_front(ptr);
      ptr->doThisWhenReady();
    } else {
      c.next();
    }
  }
}

void YSE::SOUND::managerObject::syncAndReleaseInUse() {
  for (auto c = inUse.front(); c.valid();) {
    implementationObject* ptr = c.get();
    ptr->sync();
    if (ptr->getStatus() == OBJECT_RELEASE) {
      c.erase();
      // Audio-thread-side disconnect: remove this impl from parent->sounds
      // BEFORE marking it OBJECT_DELETE. The slow-pool's deleteJob filters
      // on OBJECT_DELETE, so any impl visible to it has already been pulled
      // from the audio-thread-iterated `sounds` list — no race on
      // sounds.remove() in the destructor.
      if (ptr->parent != nullptr &&
          ptr->connectedToParent.load(
              std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with
                                            // release in doThisWhenReady()
        ptr->parent->disconnect(ptr);
        ptr->connectedToParent.store(
            false, std::memory_order_release); // NOSONAR S8417: intentional release — publishes
                                               // audio-thread disconnect before slow-pool delete
      }
      // Defensive: null the user-supplied DSP source pointer before the
      // impl becomes eligible for destruction. If the user destroyed their
      // dspSourceObject slightly before this point, the audio thread's
      // dsp() will now load nullptr instead of a dangling pointer.
      ptr->source_dsp.store(
          nullptr, std::memory_order_release); // NOSONAR S8417: intentional release — publishes
                                               // nulled dsp source to audio thread's acquire load
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue; // c already refers to the successor after erase()
    }
    ptr->update();
    c.next();
  }
}

void YSE::SOUND::managerObject::update() {
  // drain the main→audio inbox of newly-set-up impls
  drainInbox();

  // Hand soundFile garbage collection to the slow pool (issue #186). The audio
  // thread must not iterate or erase `soundFiles`: erasing runs ~soundFile
  // (sf_close + delete[]) on the callback and races addFile on the main thread.
  // Throttle to roughly once a second so the GC job isn't re-queued every tick.
  fileGCTimer += INTERNAL::Time().delta();
  if (fileGCTimer >= 1.0f && !mgrFileGC.isQueued()) {
    fileGCTimer = 0.f;
    INTERNAL::Global().addSlowJob(&mgrFileGC);
  }

  VirtualSoundFinder().reset();

  if (!toLoad.empty() && !mgrSetup.isQueued()) {
    scrubToLoadAndScheduleSetup();
  }

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  promoteReadyImpls();
  syncAndReleaseInUse();

  VirtualSoundFinder().calculate();
}

void YSE::SOUND::managerObject::garbageCollectFiles() {
  // Runs on the slow pool (mgrFileGC), never the audio thread (issue #186).
  // Measure the wall time elapsed since the previous pass so the per-file idle
  // timer advances at the same rate it did when this loop ran every audio
  // callback (it accumulated Time().delta() there). ~soundFile — with its
  // sf_close and delete[] — now runs here on the erase, off the callback.
  std::clock_t now = std::clock();
  Flt dt = (lastGCClock == 0)
               ? 0.f
               : static_cast<Flt>(now - lastGCClock) / static_cast<Flt>(CLOCKS_PER_SEC);
  lastGCClock = now;

  std::scoped_lock lk(soundFilesMutex);
  auto iMinus = soundFiles.before_begin();
  for (auto i = soundFiles.begin(); i != soundFiles.end();) {
    if (i->isQueued() || i->inUse(dt)) {
      iMinus = i;
      ++i;
    } else {
      i = soundFiles.erase_after(iMinus);
    }
  }
}

Bool YSE::SOUND::managerObject::empty() {
  // Called only from the audio callback (deviceManager::doOnCallback). It must
  // read ONLY audio-thread-owned state: `toLoad` and `inUse` are single-thread
  // (audio) by design, whereas `implementations` is mutated by the main thread
  // (addImplementation) and the slow-pool (deleteJob) under implementationsMutex.
  // Reading `implementations`' head from the callback without that lock was a
  // data race (issue #200). An impl becomes audible only once it has been
  // drained from the inbox into `toLoad` and promoted into `inUse`, so the
  // combined emptiness of those two lists is the audio thread's authoritative
  // "nothing to render" signal.
  return toLoad.empty() && inUse.empty();
}

/*AudioFormatReader * YSE::SOUND::managerObject::getReader(const File & f) {
  return formatManager.createReaderFor(f);
}

AudioFormatReader * YSE::SOUND::managerObject::getReader(juce::InputStream * source) {
  return formatManager.createReaderFor(source);
}*/

void YSE::SOUND::managerObject::adjustLastGainBuffer() {
  for (auto i = inUse.begin(); i != inUse.end(); ++i) {
    UInt j =
        static_cast<UInt>((*i)->lastGain.size()); // need to store previous size for deep resize
    (*i)->lastGain.resize(CHANNEL::Manager().getNumberOfOutputs());
    for (; j < (*i)->lastGain.size(); j++) {
      (*i)->lastGain[j].resize((*i)->buffer->size(), 0.0f);
    }
  }
}
