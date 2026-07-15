/*
  ==============================================================================

    clipManager.cpp
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CLIP::managerObject& YSE::CLIP::Manager() {
  static managerObject m;
  return m;
}

YSE::CLIP::managerObject::managerObject() : mgrDelete(this), runDelete(false) {}

YSE::CLIP::managerObject::~managerObject() noexcept {
  try {
    mgrDelete.join();
    transport* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }
    inUse.clear();
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CLIP::Manager destructor swallowed exception");
  }
}

YSE::CLIP::transport* YSE::CLIP::managerObject::addImplementation(clip* head) {
  transport* impl = nullptr;
  {
    std::scoped_lock lk(implementationsMutex);
    implementations.emplace_front(head);
    impl = &implementations.front();
  }
  // Hand the impl to the audio thread via the lock-free inbox — the audio
  // thread owns `inUse` and decides when the transport starts advancing.
  toLoadInbox.push(impl);
  return impl;
}

void YSE::CLIP::managerObject::update() {
  ///////////////////////////////////////////
  // drain the control->audio inbox of newly-created transports
  ///////////////////////////////////////////
  {
    transport* p;
    while (toLoadInbox.try_pop(p))
      inUse.emplace_front(p);
  }

  ///////////////////////////////////////////
  // enqueue the slow-pool delete job for transports retired last tick. The
  // one-tick defer guarantees the orphan is out of `inUse` before the slow pool
  // can free it — no audio-thread free, no dangling `inUse` entry.
  ///////////////////////////////////////////
  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // advance each transport; retire orphans (interface destroyed) from the
  // working list. A retired transport is flagged OBJECT_DELETE and left in
  // `implementations` for the slow-pool deleteJob to reap.
  ///////////////////////////////////////////
  auto previous = inUse.before_begin();
  for (auto i = inUse.begin(); i != inUse.end();) {
    if (!(*i)->hasInterface()) {
      transport* ptr = *i;
      i = inUse.erase_after(previous);
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue;
    }
    (*i)->advance();
    previous = i;
    ++i;
  }
}

void YSE::CLIP::managerObject::clear() {
  try {
    mgrDelete.join();
    transport* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }
    inUse.clear();
    std::scoped_lock lk(implementationsMutex);
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CLIP::Manager clear swallowed exception");
  }
}
