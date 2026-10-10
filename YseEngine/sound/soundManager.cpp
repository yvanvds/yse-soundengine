/*
  ==============================================================================

    soundLoader.cpp
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#include "soundManager.h"
#include <cassert>
#include <iterator>
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
    INTERNAL::EmitNoThrow(E_ERROR, "SOUND::Manager destructor swallowed exception");
  }
}

void YSE::SOUND::managerObject::destroy() {
  // Runs from system::close() after both thread pools are joined and the audio
  // device is closed, with Global().active already false. Drains every
  // lingering sound impl BEFORE CHANNEL::Manager().destroy() frees the channel
  // impls, so no sound impl is left holding a dangling `parent` pointer — the
  // root of the static-teardown / re-init use-after-free (issue #298). An impl
  // whose interface was destroyed while the engine was up but that never got
  // pumped to OBJECT_DELETE before close() lingers here otherwise: at the next
  // init() the audio thread would reprocess it (parent already freed), and at
  // static exit its destructor would disconnect from freed channel storage.
  // Mirrors CHANNEL::Manager().destroy() (issue #132) and the "no persistence
  // across init/close" guarantee (issue #121).

  // No-ops once the pools are down, but mirror the destructor's contract that
  // no setup/delete/GC job is mid-flight before the lists are torn.
  mgrSetup.join();
  mgrDelete.join();
  mgrFileGC.join();

  // Drain the main->audio inbox; the pointers it holds reference impls owned by
  // `implementations` and would dangle once that list is cleared below.
  implementationObject* drained;
  while (toLoadInbox.try_pop(drained)) {
    (void)drained;
  }
  toLoad.clear();
  inUse.clear();
  {
    // Clear `implementations` before `soundFiles`: each impl destructor calls
    // file->release(this) on its (shared) soundFile, which must still be alive.
    std::scoped_lock lk(implementationsMutex);
    implementations.clear();
  }
  {
    std::scoped_lock lk(soundFilesMutex);
    soundFiles.clear();
  }
  runDelete = false;
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

void YSE::SOUND::managerObject::releaseUnpublished(YSE::SOUND::implementationObject* impl) {
  // A create() that was refused (missing file, patcher already owned) drops its
  // handle without ever calling setup(), so this impl exists in
  // `implementations` and nowhere else. Before #817 it was flagged
  // OBJECT_RELEASE, which nothing promotes for an impl that never reached
  // `inUse` — so it sat in the list for the rest of the session and a caller
  // retrying a bad asset grew the list without bound.
  //
  // OBJECT_DELETE is the state the slow-pool delete job filters on, and going
  // there directly is safe precisely because the impl was never published: the
  // audio thread holds no pointer to it (it is in neither toLoadInbox, toLoad
  // nor inUse), no channel has it in `sounds` (connectedToParent is false), and
  // the slow-pool setup job cannot claim it (tryClaimForSetup CASes from
  // OBJECT_CREATED, which this impl never reached). The destructor therefore
  // still runs where every other impl's does — on the slow pool, under
  // implementationsMutex — and not on this control thread.
  assert(impl->getStatus() == OBJECT_CONSTRUCTED &&
         "releaseUnpublished() is only for an impl that never reached setup()");
  impl->setStatus(OBJECT_DELETE);
  runDelete = true;
}

std::size_t YSE::SOUND::managerObject::implementationCount() {
  std::scoped_lock lk(implementationsMutex);
  return static_cast<std::size_t>(std::distance(implementations.begin(), implementations.end()));
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
      // Unlink FIRST, publish OBJECT_DELETE second (issue #830). The store is
      // what makes this impl match managerDeleteJob's canBeDeleted predicate,
      // and a delete job queued by an earlier tick can still be inside
      // remove_if right now — update()'s isQueued() guard only prevents
      // queueing the same job object twice, it never joins a job in flight. In
      // the reverse order the slow pool could free the impl between the store
      // and the erase, and cursor::erase() then wrote through the freed node's
      // `_mgrNext`. Erasing while the impl is still OBJECT_DELETE_PENDING (a
      // state the delete job ignores) keeps the audio-thread-only list
      // stitched before the pool is allowed to look at the node at all; the
      // release store then publishes the finished unlink to the job's load.
      c.erase();
      p->objectStatus.store(
          OBJECT_DELETE,
          std::memory_order_release); // NOSONAR S8417: intentional release — publish DELETE state
                                      // to slow-pool deleteJob's acquire load
      runDelete = true;
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
      // Detach the post-DSP back-reference HERE, on the update/audio thread
      // where addDSP() also runs, instead of in the slow-pool destructor
      // (issue #838). The destructor's clearing raced a concurrent addDSP()
      // re-attaching the same dspObject to another sound (TSan: addDSP's
      // `calledfrom = &post_dsp` store vs. the delete job's `calledfrom =
      // nullptr`). On this thread the ownership test is exact: clear the
      // back-reference only while it still points at our own slot, so a
      // plugin already re-attached elsewhere keeps its fresh back-reference.
      // Nulling post_dsp afterwards makes the destructor's fallback a no-op
      // on this path (it remains for engine teardown, where the audio thread
      // is already stopped); the setStatus(OBJECT_DELETE) store below
      // publishes both writes to the slow pool's status load.
      if (ptr->post_dsp != nullptr && ptr->post_dsp->calledfrom == &ptr->post_dsp) {
        ptr->post_dsp->calledfrom = nullptr;
      }
      ptr->post_dsp = nullptr;
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

  // Consume the flag atomically. Since #817 `runDelete` has two writers — the
  // audio thread below (syncAndReleaseInUse) and the control thread
  // (releaseUnpublished) — and the old test-then-unconditionally-clear would
  // drop a set that landed between the two, stranding a flagged impl in
  // `implementations` until close(): the very leak #817 is about. Exchange
  // takes the request, and if the previous delete job is still in flight (it
  // may already have walked the list before the flag was set) the request is
  // handed back for the next tick rather than dropped. Re-queueing the same
  // job object while it is queued would push one pointer into the pool ring
  // twice, so the isQueued() guard stays.
  if (runDelete.exchange(false)) {
    if (!mgrDelete.isQueued()) {
      INTERNAL::Global().addSlowJob(&mgrDelete);
    } else {
      runDelete = true;
    }
  }

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
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  Flt dt =
      haveGCClock ? static_cast<Flt>(std::chrono::duration<Dbl>(now - lastGCClock).count()) : 0.f;
  lastGCClock = now;
  haveGCClock = true;

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
