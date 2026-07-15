/*
  ==============================================================================

    midifileManager.cpp
    Created: 13 Jul 2014 5:21:20pm
    Author:  yvan

  ==============================================================================
*/

#include "midifileManager.h"
#include "../internalHeaders.h"

YSE::MIDI::managerObject& YSE::MIDI::Manager() {
  static managerObject m;
  return m;
}

YSE::MIDI::managerObject::managerObject() : mgrDelete(this), runDelete(false) {}

YSE::MIDI::managerObject::~managerObject() noexcept {
  try {
    // Wait for any in-flight delete job before tearing down the lists it reads.
    mgrDelete.join();

    // Drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and are freed when that list is cleared.
    fileImpl* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    inUse.clear();
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "MIDI::Manager destructor swallowed exception");
  }
}

YSE::MIDI::fileImpl* YSE::MIDI::managerObject::addImplementation(YSE::MIDI::file* head) {
  fileImpl* impl = nullptr;
  {
    std::scoped_lock lk(implementationsMutex);
    implementations.emplace_front(head);
    impl = &implementations.front();
  }
  // Hand the impl off to the audio thread via the lock-free inbox. The audio
  // thread owns the working `inUse` list, so it — not the main thread — decides
  // when the impl participates in update() (issue #190).
  toLoadInbox.push(impl);
  return impl;
}

void YSE::MIDI::managerObject::update() {
  ///////////////////////////////////////////
  // drain the main→audio inbox of newly-created impls into the working list
  ///////////////////////////////////////////
  {
    fileImpl* p;
    while (toLoadInbox.try_pop(p))
      inUse.push_front(p);
  }

  ///////////////////////////////////////////
  // enqueue the slow-pool delete job for impls retired on the previous tick.
  // Deferring by a tick guarantees the orphan is already out of `inUse` before
  // the slow pool can free it — no audio-thread free, no dangling `inUse` entry.
  ///////////////////////////////////////////
  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // retire orphaned impls (interface destroyed) from the working list. The
  // impl is flagged OBJECT_DELETE and left in `implementations` for the
  // slow-pool deleteJob to reap — it is never freed on the audio thread.
  ///////////////////////////////////////////
  for (auto c = inUse.front(); c.valid();) {
    fileImpl* ptr = c.get();
    if (!ptr->hasInterface()) {
      c.erase();
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue; // c already refers to the successor after erase()
    }
    c.next();
  }
}

void YSE::MIDI::managerObject::updatePlayback(int numSamples) {
  // Audio-thread-owned working list — safe to walk here (same thread that
  // drains it in update()). Each file advances its own clock and pushes any
  // events for this block onto its connected synths' inboxes.
  for (fileImpl* impl : inUse) {
    impl->advance(numSamples);
  }
}
