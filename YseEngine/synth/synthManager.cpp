/*
  ==============================================================================

    synthManager.cpp
    Synth subsystem manager singleton. See synthManager.h and
    docs/design/synth_core.md §8.

  ==============================================================================
*/

#include "synthManager.h"
#include "../internalHeaders.h"

YSE::SYNTH::managerObject& YSE::SYNTH::Manager() {
  static managerObject m;
  return m;
}

YSE::SYNTH::managerObject::managerObject() : mgrSetup(this), mgrDelete(this) {}

YSE::SYNTH::managerObject::~managerObject() noexcept {
  try {
    mgrSetup.join();
    mgrDelete.join();

    // Drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and are freed when that list is cleared.
    implementationObject* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    toLoad.clear();
    inUse.clear();
    {
      std::scoped_lock lk(implementationsMutex);
      implementations.clear();
    }
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "SYNTH::Manager destructor swallowed exception");
  }
}

YSE::SYNTH::implementationObject*
YSE::SYNTH::managerObject::addImplementation(YSE::SYNTH::interfaceObject* head) {
  std::scoped_lock lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::SYNTH::managerObject::setup(YSE::SYNTH::implementationObject* impl) {
  impl->setStatus(OBJECT_CREATED);
  // Hand off to the audio thread via the lock-free inbox; update() drains it
  // into `toLoad` and schedules the setup-pool clone job.
  toLoadInbox.push(impl);
}

void YSE::SYNTH::managerObject::drainInbox() {
  implementationObject* p;
  while (toLoadInbox.try_pop(p))
    toLoad.push_front(p);
}

void YSE::SYNTH::managerObject::promoteReadyImpls() {
  // Move OBJECT_SETUP impls into inUse, erasing from toLoad in the same step so
  // no freed pointer lingers in the audio-thread-iterated list (same
  // use-after-free rationale as the SOUND/REVERB managers). toLoad and inUse
  // share the impl's single `_mgrNext` link, so unlink before relink.
  for (auto c = toLoad.front(); c.valid();) {
    implementationObject* ptr = c.get();
    if (ptr->readyCheck()) {
      c.erase();
      inUse.push_front(ptr);
    } else {
      c.next();
    }
  }
}

void YSE::SYNTH::managerObject::syncAndReleaseInUse() {
  for (auto c = inUse.front(); c.valid();) {
    implementationObject* ptr = c.get();
    ptr->sync();
    if (ptr->getStatus() == OBJECT_RELEASE) {
      // The synth impl's only audio-visible reference is its outputSource,
      // held by the owning sound. Per the §9 lifetime contract the synth
      // outlives its sound, so by the time the interface is gone no sound
      // still points at us. Move straight to OBJECT_DELETE for the delete pool.
      c.erase();
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue; // c already refers to the successor after erase()
    }
    c.next();
  }
}

void YSE::SYNTH::managerObject::update() {
  drainInbox();

  if (!toLoad.empty() && !mgrSetup.isQueued()) {
    toLoad.remove_if(implementationObject::canBeRemovedFromLoading);
    INTERNAL::Global().addSlowJob(&mgrSetup);
  }

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  promoteReadyImpls();
  syncAndReleaseInUse();
}

Bool YSE::SYNTH::managerObject::empty() {
  return toLoad.empty() && inUse.empty();
}
