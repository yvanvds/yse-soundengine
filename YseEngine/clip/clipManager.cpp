/*
  ==============================================================================

    clipManager.cpp
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#include <iterator>

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
    INTERNAL::EmitNoThrow(E_ERROR, "CLIP::Manager destructor swallowed exception");
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
    // Only orphans (interface already destroyed) are freed. A transport whose
    // YSE::clip is still alive must outlive the session: the clip's pimpl points
    // at it, and its destructor or any later call would otherwise touch freed
    // memory (issue #974). The survivor is handed back to the (now idle) inbox
    // so the next session's audio thread picks it up — it keeps working after a
    // re-init, and once its interface goes it is retired and reaped through the
    // normal update() / slow-pool path. The transport's clock share (#707) keeps
    // the clock it points at alive past CLOCK::Manager().clear(); that clock no
    // longer advances, so the transport idles until rebound.
    implementations.remove_if([](const transport& t) { return !t.hasInterface(); });
    for (auto& t : implementations)
      toLoadInbox.push(&t);
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CLIP::Manager clear swallowed exception");
  }
}

std::size_t YSE::CLIP::managerObject::implementationCountForTest() {
  std::scoped_lock lk(implementationsMutex);
  return static_cast<std::size_t>(std::distance(implementations.begin(), implementations.end()));
}
