/*
  ==============================================================================

    playerManager.cpp
    Created: 9 Apr 2015 1:39:02pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::PLAYER::managerObject& YSE::PLAYER::Manager() {
  static managerObject m;
  return m;
}

YSE::PLAYER::managerObject::managerObject() : mgrDelete(this), runDelete(false) {}

YSE::PLAYER::managerObject::~managerObject() noexcept {
  try {
    // Wait for any in-flight delete job before tearing down the lists it reads.
    mgrDelete.join();

    // Drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and are freed when that list is cleared.
    implementationObject* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    inUse.clear();
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "PLAYER::Manager destructor swallowed exception");
  }
}

YSE::PLAYER::implementationObject*
YSE::PLAYER::managerObject::addImplementation(YSE::player* head,
                                              YSE::SYNTH::interfaceObject* instrument) {
  implementationObject* impl = nullptr;
  {
    std::scoped_lock lk(implementationsMutex);
    implementations.emplace_front(head, instrument);
    impl = &implementations.front();
  }
  // Hand the impl off to the audio thread via the lock-free inbox. The audio
  // thread owns the working `inUse` list, so it — not the main thread — decides
  // when the impl participates in update() (issue #190).
  toLoadInbox.push(impl);
  return impl;
}

void YSE::PLAYER::managerObject::update(Flt delta) {
  ///////////////////////////////////////////
  // drain the main->audio inbox of newly-created impls into the working list
  ///////////////////////////////////////////
  {
    implementationObject* p;
    while (toLoadInbox.try_pop(p))
      inUse.emplace_front(p);
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
  // advance generation, and retire orphaned impls (interface destroyed) from the
  // working list. A retired impl is flagged OBJECT_DELETE and left in
  // `implementations` for the slow-pool deleteJob to reap — it is never freed on
  // the audio thread. Fixes the previous erase_after + ++i iterator bug (#268).
  ///////////////////////////////////////////
  auto previous = inUse.before_begin();
  for (auto i = inUse.begin(); i != inUse.end();) {
    if (!(*i)->update(delta)) {
      implementationObject* ptr = *i;
      i = inUse.erase_after(previous);
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue;
    }
    previous = i;
    ++i;
  }
}
