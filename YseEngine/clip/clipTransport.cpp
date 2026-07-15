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

#if YSE_ENABLE_MIDI_DEVICE
  // Audio-thread sink that fans one fired note out to every connected external
  // MIDI-out port (issue #350). Each call encodes the wire bytes, stamps them
  // with the block's absolute send deadline, and try_pushes onto the sender's
  // bounded lock-free queue — no allocation, no lock, no RtMidi on this thread.
  // A full queue drops the message rather than block.
  struct midiOutFanout {
    std::array<std::atomic<RtMidiOut*>, 4>* ports;
    YSE::MIDI::outSender* sender;
    std::int64_t dueNs;

    void noteOn(int channel, int pitch, float velocity) {
      for (auto& slot : *ports) {
        RtMidiOut* p = slot.load(std::memory_order_acquire);
        if (p != nullptr)
          sender->tryEnqueue(YSE::MIDI::makeNoteOn(p, dueNs, channel, pitch, velocity));
      }
    }
    void noteOff(int channel, int pitch, float velocity) {
      for (auto& slot : *ports) {
        RtMidiOut* p = slot.load(std::memory_order_acquire);
        if (p != nullptr)
          sender->tryEnqueue(YSE::MIDI::makeNoteOff(p, dueNs, channel, pitch, velocity));
      }
    }
    void pitchWheel(int channel, float value) {
      for (auto& slot : *ports) {
        RtMidiOut* p = slot.load(std::memory_order_acquire);
        if (p != nullptr) sender->tryEnqueue(YSE::MIDI::makePitchWheel(p, dueNs, channel, value));
      }
    }
  };
#else
  struct midiOutFanout { // MIDI device backend compiled out — no-op sink half
    void noteOn(int, int, float) {}
    void noteOff(int, int, float) {}
    void pitchWheel(int, float) {}
  };
#endif

  // The transport's real output sink: internal synth inboxes + external
  // MIDI-out queue, both fed from the same evaluateWindow firing.
  struct clipSink {
    synthFanout synths;
    midiOutFanout midi;

    void noteOn(int channel, int pitch, float velocity) {
      synths.noteOn(channel, pitch, velocity);
      midi.noteOn(channel, pitch, velocity);
    }
    void noteOff(int channel, int pitch, float velocity) {
      synths.noteOff(channel, pitch, velocity);
      midi.noteOff(channel, pitch, velocity);
    }
    void pitchWheel(int channel, float value) {
      synths.pitchWheel(channel, value);
      midi.pitchWheel(channel, value);
    }
  };

} // namespace

YSE::CLIP::transport::transport(clip* head)
  : head(head), objectStatus(OBJECT_READY), intent(SS_STOPPED), loopBeats(0.0), incoming(nullptr) {
  // std::atomic has no zero-initializing default constructor (pre-C++20), so the
  // synth table must be explicitly cleared before the audio thread reads it.
  for (auto& slot : synths)
    slot.store(nullptr, std::memory_order_relaxed);
#if YSE_ENABLE_MIDI_DEVICE
  for (auto& slot : midiPorts)
    slot.store(nullptr, std::memory_order_relaxed);
#endif
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

#if YSE_ENABLE_MIDI_DEVICE

void YSE::CLIP::transport::connectMidiOut(RtMidiOut* port) {
  if (port == nullptr) return;
  int freeIdx = -1;
  for (std::size_t i = 0; i < kMaxMidiOuts; ++i) {
    RtMidiOut* cur = midiPorts[i].load(std::memory_order_acquire);
    if (cur == port) return; // already connected
    if (cur == nullptr && freeIdx < 0) freeIdx = static_cast<int>(i);
  }
  if (freeIdx < 0) return; // table full — ignore rather than grow a lock-free table
  // Spin up the sender before the audio thread can see the port, so the first
  // fired events find a live consumer (they would only be delayed, not lost,
  // but the ordering keeps the contract simple).
  MIDI::OutSender().start();
  midiPorts[static_cast<std::size_t>(freeIdx)].store(port, std::memory_order_release);
}

void YSE::CLIP::transport::disconnectMidiOut(RtMidiOut* port) {
  if (port == nullptr) return;
  for (auto& slot : midiPorts) {
    if (slot.load(std::memory_order_acquire) == port) {
      slot.store(nullptr, std::memory_order_release);
      return;
    }
  }
}

#endif // YSE_ENABLE_MIDI_DEVICE

void YSE::CLIP::transport::advance() {
  clipSink sink{};
  sink.synths.synths = &synths;
#if YSE_ENABLE_MIDI_DEVICE
  sink.midi.ports = &midiPorts;
  sink.midi.sender = &MIDI::OutSender();
  // Absolute send deadline shared by every event this block fires: paced one
  // block per callback, resynced to `now` when the callback fell behind. Only
  // stamped when a port is connected — no clock read on the pure-synth path.
  bool anyMidi = false;
  for (auto& slot : midiPorts) {
    if (slot.load(std::memory_order_relaxed) != nullptr) {
      anyMidi = true;
      break;
    }
  }
  if (anyMidi) {
    const std::int64_t now = MIDI::nowNs();
    sink.midi.dueNs = nextDueNs > now ? nextDueNs : now;
  }
#endif

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

#if YSE_ENABLE_MIDI_DEVICE
  if (anyMidi) {
    // Pace the next block's deadline one block ahead. Block duration derives
    // from the beat window at the clock's current tempo (a ramping tempo makes
    // this approximate — well within the block-accurate contract).
    const double tempo = clock->currentTempo();
    if (tempo > 0.0) {
      nextDueNs =
          sink.midi.dueNs + static_cast<std::int64_t>((curBeat - lastBeat) * 60.0e9 / tempo);
    }
  }
#endif

  lastBeat = curBeat;
}
