/*
  ==============================================================================

    midifileImplementation.cpp
    Created: 12 Jul 2014 7:09:29pm
    Author:  yvan

    Engine-native Standard MIDI File parser + block-accurate playback into
    synths (issue #155). Replaces the JUCE MidiFile / MidiMessageSequence
    implementation removed with the JUCE backend: create() decodes the file
    into a time-sorted event list off the audio thread, and advance() (driven
    once per callback by MIDI::managerObject::updatePlayback) pushes the events
    that fall in each block onto the connected synths' inboxes.

  ==============================================================================
*/

#include "midifileImplementation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

#include "../synth/synthInterface.hpp"
#include "midiSynthRouting.hpp"

namespace {

  // ─── low-level byte readers (all bounds-checked by the caller) ───────────────

  // Big-endian fixed-width reads. `pos` is advanced past the bytes read.
  uint16_t readU16(const std::vector<unsigned char>& d, std::size_t& pos) {
    uint16_t v = static_cast<uint16_t>((d[pos] << 8) | d[pos + 1]);
    pos += 2;
    return v;
  }

  uint32_t readU32(const std::vector<unsigned char>& d, std::size_t& pos) {
    uint32_t v = (static_cast<uint32_t>(d[pos]) << 24) | (static_cast<uint32_t>(d[pos + 1]) << 16) |
                 (static_cast<uint32_t>(d[pos + 2]) << 8) | static_cast<uint32_t>(d[pos + 3]);
    pos += 4;
    return v;
  }

  // MIDI variable-length quantity: 7 bits per byte, MSB set = "more bytes".
  // Returns false on truncation (pos past end before the value terminates).
  bool readVarLen(const std::vector<unsigned char>& d, std::size_t& pos, uint32_t& out) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      if (pos >= d.size()) return false;
      const unsigned char byte = d[pos++];
      value = (value << 7) | static_cast<uint32_t>(byte & 0x7F);
      if ((byte & 0x80) == 0) {
        out = value;
        return true;
      }
    }
    return false; // more than 4 continuation bytes is malformed
  }

  // Number of data bytes a running-status channel-voice message carries.
  int channelDataBytes(unsigned char statusNibble) {
    switch (statusNibble & 0xF0) {
    case 0xC0: // program change
    case 0xD0: // channel aftertouch
      return 1;
    default: // note on/off, poly aftertouch, CC, pitch bend
      return 2;
    }
  }

  // A tempo change: microseconds per quarter note in effect from `tick`.
  struct TempoEntry {
    uint64_t tick;
    uint32_t usPerQuarter;
    double cumUs; // accumulated microseconds at `tick`
  };

  std::vector<unsigned char> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
  }

} // namespace

YSE::MIDI::fileImpl::fileImpl(file* head)
  : head(head), intent(SS_STOPPED), objectStatus(OBJECT_READY), hasFile(false) {
  for (auto& slot : synths)
    slot.store(nullptr, std::memory_order_relaxed);
}

YSE::MIDI::fileImpl::~fileImpl() {}

bool YSE::MIDI::fileImpl::create(const std::string& fileName) {
  // Reset so create() can be re-called (main thread, before play()).
  hasFile = false;
  midiEvents.clear();
  playhead = 0;
  nextEvent = 0;

  std::vector<unsigned char> data = readWholeFile(fileName);
  if (data.empty()) {
    INTERNAL::LogImpl().emit(E_FILE_ERROR, "MIDI file not found or empty: " + fileName);
    return false;
  }

  std::size_t pos = 0;

  // ---- header chunk (MThd) -------------------------------------------------
  if (data.size() < 14 || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
    INTERNAL::LogImpl().emit(E_FILE_ERROR, "Not a standard MIDI file: " + fileName);
    return false;
  }
  pos = 4;
  const uint32_t headerLen = readU32(data, pos);
  const std::size_t headerEnd = pos + headerLen;
  if (headerLen < 6 || headerEnd > data.size()) {
    INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI header: " + fileName);
    return false;
  }
  pos += 2; // format — Type 0/1/2 are all parsed the same way (merge tracks by time)
  const uint16_t numTracks = readU16(data, pos);
  const uint16_t division = readU16(data, pos);
  pos = headerEnd; // skip any extra header bytes

  // Tick -> time conversion. PPQN (tempo-driven) is the common case; SMPTE is
  // a fixed tick rate independent of tempo.
  const bool smpte = (division & 0x8000) != 0;
  double smpteSamplesPerTick = 0.0;
  uint32_t ppqn = 96;
  if (smpte) {
    const int framesPerSecond = -static_cast<int8_t>(division >> 8);
    const int ticksPerFrame = division & 0xFF;
    const double ticksPerSecond = static_cast<double>(framesPerSecond) * ticksPerFrame;
    if (ticksPerSecond > 0.0)
      smpteSamplesPerTick = static_cast<double>(SAMPLERATE) / ticksPerSecond;
  } else {
    ppqn = division & 0x7FFF;
    if (ppqn == 0) ppqn = 96; // guard against a zero division
  }

  // ---- decode every track into (tick, event) pairs -------------------------
  struct RawEvent {
    uint64_t tick;
    unsigned char status;
    unsigned char data1;
    unsigned char data2;
  };
  std::vector<RawEvent> raw;
  std::vector<TempoEntry> tempo;

  for (uint16_t t = 0; t < numTracks; ++t) {
    if (pos + 8 > data.size()) break; // no room for another chunk header
    const bool isTrack =
        data[pos] == 'M' && data[pos + 1] == 'T' && data[pos + 2] == 'r' && data[pos + 3] == 'k';
    pos += 4;
    const uint32_t trackLen = readU32(data, pos);
    const std::size_t trackEnd = pos + trackLen;
    if (trackEnd > data.size()) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "Truncated MIDI track: " + fileName);
      return false;
    }
    if (!isTrack) {
      pos = trackEnd; // unknown chunk type — skip it whole
      continue;
    }

    uint64_t tick = 0;
    unsigned char runningStatus = 0;

    while (pos < trackEnd) {
      uint32_t delta = 0;
      if (!readVarLen(data, pos, delta) || pos >= trackEnd) {
        INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI delta-time: " + fileName);
        return false;
      }
      tick += delta;

      unsigned char statusByte = data[pos];
      if (statusByte & 0x80) {
        ++pos;
      } else {
        // Running status: reuse the previous channel-voice status byte.
        statusByte = runningStatus;
        if ((statusByte & 0x80) == 0) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "MIDI running status without status: " + fileName);
          return false;
        }
      }

      if (statusByte == 0xFF) {
        // Meta event: FF type len data...
        if (pos >= trackEnd) return false;
        const unsigned char metaType = data[pos++];
        uint32_t metaLen = 0;
        if (!readVarLen(data, pos, metaLen) || pos + metaLen > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI meta event: " + fileName);
          return false;
        }
        if (metaType == 0x51 && metaLen == 3) { // set tempo (us per quarter)
          const uint32_t us = (static_cast<uint32_t>(data[pos]) << 16) |
                              (static_cast<uint32_t>(data[pos + 1]) << 8) |
                              static_cast<uint32_t>(data[pos + 2]);
          tempo.push_back({tick, us, 0.0});
        }
        pos += metaLen;
        runningStatus = 0; // meta cancels running status
      } else if (statusByte == 0xF0 || statusByte == 0xF7) {
        // SysEx (or escape): F0/F7 len data... — skipped.
        uint32_t sysexLen = 0;
        if (!readVarLen(data, pos, sysexLen) || pos + sysexLen > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI sysex event: " + fileName);
          return false;
        }
        pos += sysexLen;
        runningStatus = 0; // sysex cancels running status
      } else {
        // Channel-voice message.
        runningStatus = statusByte;
        const int nBytes = channelDataBytes(statusByte);
        if (pos + static_cast<std::size_t>(nBytes) > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Truncated MIDI channel message: " + fileName);
          return false;
        }
        const unsigned char d1 = data[pos];
        const unsigned char d2 = nBytes == 2 ? data[pos + 1] : 0;
        pos += static_cast<std::size_t>(nBytes);
        raw.push_back({tick, statusByte, d1, d2});
      }
    }
    pos = trackEnd; // resynchronise to the declared chunk boundary
  }

  // ---- build the tempo map and convert ticks to samples --------------------
  std::stable_sort(tempo.begin(), tempo.end(),
                   [](const TempoEntry& a, const TempoEntry& b) { return a.tick < b.tick; });
  if (tempo.empty() || tempo.front().tick != 0) {
    tempo.insert(tempo.begin(), {0, 500000, 0.0}); // default 120 BPM before any change
  }
  tempo.front().cumUs = 0.0;
  for (std::size_t i = 1; i < tempo.size(); ++i) {
    const uint64_t deltaTicks = tempo[i].tick - tempo[i - 1].tick;
    tempo[i].cumUs =
        tempo[i - 1].cumUs + static_cast<double>(deltaTicks) * tempo[i - 1].usPerQuarter / ppqn;
  }

  auto tickToSample = [&](uint64_t targetTick) -> uint64_t {
    if (smpte)
      return static_cast<uint64_t>(
          std::llround(static_cast<double>(targetTick) * smpteSamplesPerTick));
    // Last tempo entry whose tick <= targetTick.
    std::size_t idx = 0;
    for (std::size_t i = 0; i < tempo.size(); ++i) {
      if (tempo[i].tick <= targetTick)
        idx = i;
      else
        break;
    }
    const double us = tempo[idx].cumUs + static_cast<double>(targetTick - tempo[idx].tick) *
                                             tempo[idx].usPerQuarter / ppqn;
    return static_cast<uint64_t>(std::llround(us * static_cast<double>(SAMPLERATE) / 1'000'000.0));
  };

  midiEvents.reserve(raw.size());
  for (const RawEvent& e : raw)
    midiEvents.push_back({tickToSample(e.tick), e.status, e.data1, e.data2});

  // Stable sort so simultaneous events keep their in-file (track) order.
  std::stable_sort(
      midiEvents.begin(), midiEvents.end(),
      [](const fileEvent& a, const fileEvent& b) { return a.sampleTime < b.sampleTime; });

  hasFile = true;
  return true;
}

void YSE::MIDI::fileImpl::play() {
  intent.store(SS_WANTSTOPLAY, std::memory_order_release);
}

void YSE::MIDI::fileImpl::pause() {
  intent.store(SS_WANTSTOPAUSE, std::memory_order_release);
}

void YSE::MIDI::fileImpl::stop() {
  intent.store(SS_WANTSTOSTOP, std::memory_order_release);
}

// ---- synth wiring (main thread) --------------------------------------------

void YSE::MIDI::fileImpl::connect(SYNTH::interfaceObject* target) {
  if (target == nullptr) return;
  int freeIdx = -1;
  for (std::size_t i = 0; i < kMaxSynths; ++i) {
    SYNTH::interfaceObject* cur = synths[i].load(std::memory_order_acquire);
    if (cur == target) return; // already connected
    if (cur == nullptr && freeIdx < 0) freeIdx = static_cast<int>(i);
  }
  if (freeIdx < 0) {
    INTERNAL::LogImpl().emit(E_WARNING,
                             "MIDI::file: synth connection table full, ignoring connect");
    return;
  }
  // Release so advance()'s acquire-load sees a fully-published target pointer.
  synths[static_cast<std::size_t>(freeIdx)].store(target, std::memory_order_release);
}

void YSE::MIDI::fileImpl::disconnect(SYNTH::interfaceObject* target) {
  if (target == nullptr) return;
  for (auto& slot : synths) {
    if (slot.load(std::memory_order_acquire) == target) {
      slot.store(nullptr, std::memory_order_release);
      return;
    }
  }
}

// ---- playback (audio thread) -----------------------------------------------

void YSE::MIDI::fileImpl::advance(int numSamples) {
  switch (intent.load(std::memory_order_acquire)) {
  case SS_WANTSTOPLAY:
    intent.store(SS_PLAYING, std::memory_order_release);
    break; // start emitting this block
  case SS_PLAYING:
    break;
  case SS_WANTSTOPAUSE:
    allNotesOffToSynths();
    intent.store(SS_PAUSED, std::memory_order_release);
    return;
  case SS_WANTSTOSTOP:
    allNotesOffToSynths();
    playhead = 0;
    nextEvent = 0;
    intent.store(SS_STOPPED, std::memory_order_release);
    return;
  default: // SS_STOPPED / SS_PAUSED — idle
    return;
  }

  if (!hasFile || numSamples <= 0) return;

  const uint64_t blockEnd = playhead + static_cast<uint64_t>(numSamples);
  while (nextEvent < midiEvents.size() && midiEvents[nextEvent].sampleTime < blockEnd) {
    dispatchEvent(midiEvents[nextEvent]);
    ++nextEvent;
  }
  playhead = blockEnd;

  if (nextEvent >= midiEvents.size()) {
    // One-shot playback: release held notes and rewind to the start.
    allNotesOffToSynths();
    playhead = 0;
    nextEvent = 0;
    intent.store(SS_STOPPED, std::memory_order_release);
  }
}

void YSE::MIDI::fileImpl::dispatchEvent(const fileEvent& event) {
  const unsigned char statusNibble = event.status & 0xF0;
  const unsigned char channelNibble = event.status & 0x0F;
  for (auto& slot : synths) {
    SYNTH::interfaceObject* s = slot.load(std::memory_order_acquire);
    if (s != nullptr)
      routeChannelVoiceMessage(*s, statusNibble, channelNibble, event.data1, event.data2);
  }
}

void YSE::MIDI::fileImpl::allNotesOffToSynths() {
  for (auto& slot : synths) {
    SYNTH::interfaceObject* s = slot.load(std::memory_order_acquire);
    if (s != nullptr) s->allNotesOff(0);
  }
}

// ---- lifecycle -------------------------------------------------------------

void YSE::MIDI::fileImpl::removeInterface() {
  head.store(nullptr);
}

bool YSE::MIDI::fileImpl::hasInterface() {
  return (head.load() != nullptr);
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::MIDI::fileImpl::getStatus() const {
  return objectStatus.load();
}

void YSE::MIDI::fileImpl::setStatus(YSE::OBJECT_IMPLEMENTATION_STATE value) {
  objectStatus.store(value);
}
