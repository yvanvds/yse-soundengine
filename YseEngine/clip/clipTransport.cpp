/*
  ==============================================================================

    clipTransport.cpp
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#include "clipTransport.h"

#include "../clock/clockManager.h"
#include "../clock/domainClock.h"
#include "../synth/synthInterface.hpp"

namespace {

  // Audio-thread sink that fans one fired note out to every connected synth.
  // Reads the transport's atomic synth table; each synth call is a lock-free
  // push onto the synth's inbox (RT-safe, mirrors MIDI-file playback).
  struct synthFanout {
    std::array<std::atomic<YSE::SYNTH::interfaceObject*>, 8>* synths;

    void noteOn(int channel, int pitch, float velocity) {
      for (auto& slot : *synths) {
        YSE::SYNTH::interfaceObject* s = slot.load(std::memory_order_acquire);
        if (s != nullptr) s->noteOn(channel, pitch, velocity);
      }
    }
    void noteOff(int channel, int pitch, float velocity) {
      for (auto& slot : *synths) {
        YSE::SYNTH::interfaceObject* s = slot.load(std::memory_order_acquire);
        if (s != nullptr) s->noteOff(channel, pitch, velocity);
      }
    }
    void pitchWheel(int channel, float value) {
      for (auto& slot : *synths) {
        YSE::SYNTH::interfaceObject* s = slot.load(std::memory_order_acquire);
        if (s != nullptr) s->pitchWheel(channel, value);
      }
    }
  };

} // namespace

YSE::CLIP::transport::transport(clip* head)
  : head(head), objectStatus(OBJECT_READY), intent(SS_STOPPED), loopBeats(0.0), incoming(nullptr) {
  // std::atomic has no zero-initializing default constructor (pre-C++20), so the
  // synth table must be explicitly cleared before the audio thread reads it.
  for (auto& slot : synths)
    slot.store(nullptr, std::memory_order_relaxed);
}

YSE::CLIP::transport::~transport() {
  // The impl is only freed by the slow-pool delete job once the audio thread
  // has retired it from `inUse`, so nothing on the audio thread still touches
  // these buffers — safe to free here.
  reclaimRetired();
  delete current;
  current = nullptr;
  delete incoming.exchange(nullptr, std::memory_order_acquire);
}

bool YSE::CLIP::transport::bind(const std::string& clockName) {
  clock = CLOCK::Manager().lookup(clockName);
  return clock != nullptr;
}

void YSE::CLIP::transport::reclaimRetired() {
  const EventList* p;
  while (retired.try_pop(p))
    delete p;
}

void YSE::CLIP::transport::setEvents(const EventList& events) {
  // Reclaim any buffers the audio thread has already displaced.
  reclaimRetired();
  auto* fresh = new EventList(events);
  // Publish. If a previous list was still pending (never adopted by the audio
  // thread) exchange returns it — delete it right away; the audio thread never
  // saw it.
  const EventList* stale = incoming.exchange(fresh, std::memory_order_release);
  delete stale;
}

void YSE::CLIP::transport::connect(SYNTH::interfaceObject* target) {
  if (target == nullptr) return;
  int freeIdx = -1;
  for (std::size_t i = 0; i < kMaxSynths; ++i) {
    SYNTH::interfaceObject* cur = synths[i].load(std::memory_order_acquire);
    if (cur == target) return; // already connected
    if (cur == nullptr && freeIdx < 0) freeIdx = static_cast<int>(i);
  }
  if (freeIdx < 0) return; // table full — ignore rather than grow a lock-free table
  synths[static_cast<std::size_t>(freeIdx)].store(target, std::memory_order_release);
}

void YSE::CLIP::transport::disconnect(SYNTH::interfaceObject* target) {
  if (target == nullptr) return;
  for (auto& slot : synths) {
    if (slot.load(std::memory_order_acquire) == target) {
      slot.store(nullptr, std::memory_order_release);
      return;
    }
  }
}

void YSE::CLIP::transport::advance() {
  synthFanout sink{&synths};

  switch (intent.load(std::memory_order_acquire)) {
  case SS_WANTSTOPLAY:
    started = false; // reseed the beat window on the first playing block
    intent.store(SS_PLAYING, std::memory_order_release);
    break;
  case SS_PLAYING:
    break;
  case SS_WANTSTOSTOP:
    releaseAll(sink);
    started = false;
    intent.store(SS_STOPPED, std::memory_order_release);
    return;
  default: // SS_STOPPED / paused — idle
    return;
  }

  // Adopt a pending event list at this block boundary. Push the displaced
  // buffer to `retired` for the control thread to delete — never free here.
  const EventList* pending = incoming.exchange(nullptr, std::memory_order_acquire);
  if (pending != nullptr) {
    if (current != nullptr) retired.try_push(current);
    current = pending;
  }

  if (clock == nullptr) return;

  const double curBeat = clock->beatPosition();
  if (!started) {
    lastBeat = curBeat;
    started = true;
    return; // no firing on the seeding block
  }
  if (curBeat <= lastBeat) {
    // Clock paused (tempo 0) or running backwards: resync without firing.
    lastBeat = curBeat;
    return;
  }

  evaluateWindow(sink, lastBeat, curBeat);
  lastBeat = curBeat;
}
