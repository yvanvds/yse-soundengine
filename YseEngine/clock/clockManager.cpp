/*
  ==============================================================================

    clockManager.cpp
    Created for issue #249 — domain clocks.

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CLOCK::managerObject& YSE::CLOCK::Manager() {
  static managerObject m;
  return m;
}

YSE::CLOCK::managerObject::managerObject() : mgrDelete(this), runDelete(false) {}

YSE::CLOCK::managerObject::~managerObject() noexcept {
  try {
    // Wait for any in-flight delete job before tearing down the lists it reads.
    mgrDelete.join();

    // Drain any pointers still queued for the audio thread; they reference
    // clocks owned by `implementations` and are freed when that list is cleared.
    domainClock* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    inUse.clear();
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CLOCK::Manager destructor swallowed exception");
  }
}

YSE::CLOCK::domainClock* YSE::CLOCK::managerObject::findLive(const std::string& name) {
  for (auto& c : implementations) {
    if (!c.isReleased() && c.getName() == name) return &c;
  }
  return nullptr;
}

bool YSE::CLOCK::managerObject::createClock(const std::string& name, Flt initialTempo) {
  if (name.empty()) return false;
  std::scoped_lock lk(implementationsMutex);
  if (findLive(name) != nullptr) return false; // first registration wins
  implementations.emplace_front(name, initialTempo);
  domainClock* clock = &implementations.front();
  // Hand the clock off to the audio thread via the lock-free inbox. The audio
  // thread owns `inUse`, so it — not the control thread — decides when the clock
  // starts advancing.
  toLoadInbox.push(clock);
  return true;
}

void YSE::CLOCK::managerObject::destroyClock(const std::string& name) {
  std::scoped_lock lk(implementationsMutex);
  domainClock* clock = findLive(name);
  if (clock != nullptr) clock->release();
}

bool YSE::CLOCK::managerObject::clockExists(const std::string& name) {
  std::scoped_lock lk(implementationsMutex);
  return findLive(name) != nullptr;
}

void YSE::CLOCK::managerObject::setTempo(const std::string& name, Flt bpm, Flt rampSeconds) {
  std::scoped_lock lk(implementationsMutex);
  domainClock* clock = findLive(name);
  if (clock != nullptr) clock->requestTempo(bpm, rampSeconds);
}

Dbl YSE::CLOCK::managerObject::beatPosition(const std::string& name) {
  std::scoped_lock lk(implementationsMutex);
  domainClock* clock = findLive(name);
  return clock != nullptr ? clock->beatPosition() : 0.0;
}

Flt YSE::CLOCK::managerObject::currentTempo(const std::string& name) {
  std::scoped_lock lk(implementationsMutex);
  domainClock* clock = findLive(name);
  return clock != nullptr ? clock->currentTempo() : 0.f;
}

YSE::CLOCK::domainClock* YSE::CLOCK::managerObject::lookup(const std::string& name) {
  std::scoped_lock lk(implementationsMutex);
  return findLive(name);
}

void YSE::CLOCK::managerObject::update(Flt delta) {
  ///////////////////////////////////////////
  // drain the control->audio inbox of newly-created clocks into the working list
  ///////////////////////////////////////////
  {
    domainClock* p;
    while (toLoadInbox.try_pop(p))
      inUse.emplace_front(p);
  }

  ///////////////////////////////////////////
  // enqueue the slow-pool delete job for clocks retired on the previous tick.
  // Deferring by a tick guarantees the orphan is already out of `inUse` before
  // the slow pool can free it — no audio-thread free, no dangling `inUse` entry.
  ///////////////////////////////////////////
  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // advance each clock, and retire released clocks from the working list. A
  // retired clock is flagged OBJECT_DELETE and left in `implementations` for the
  // slow-pool deleteJob to reap — it is never freed on the audio thread.
  ///////////////////////////////////////////
  auto previous = inUse.before_begin();
  for (auto i = inUse.begin(); i != inUse.end();) {
    if (!(*i)->update(delta)) {
      domainClock* ptr = *i;
      i = inUse.erase_after(previous);
      ptr->setStatus(OBJECT_DELETE);
      runDelete = true;
      continue;
    }
    previous = i;
    ++i;
  }
}

void YSE::CLOCK::managerObject::clear() {
  try {
    mgrDelete.join();
    domainClock* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }
    inUse.clear();
    std::scoped_lock lk(implementationsMutex);
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CLOCK::Manager clear swallowed exception");
  }
}
