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
#include <cstdint>
#include <fstream>

// The header now pulls only the public umbrella (issue #266); the definitions
// this TU needs (INTERNAL::LogImpl) still come from internalHeaders.h.
#include "../internalHeaders.h"
#include "../synth/synthInterface.hpp"
#include "midiBytes.hpp"
#include "midiSynthRouting.hpp"

namespace {

  // The format's byte primitives live in midi/midiBytes.hpp since issue #698,
  // shared with the patcher's .seq reader. Only the primitives are shared — the
  // parsers are not, and that header says why. create() below is a member of
  // YSE::MIDI, so it reaches them unqualified; these two helpers are not, hence
  // the qualification here.
  //
  // Big-endian fixed-width reads over the whole-file buffer, advancing `pos` —
  // which is what the shared readers deliberately do not do, since .seq's parse
  // keeps its cursors elsewhere. The bytes must already be known to be in range:
  // every call site below checks that first.
  uint16_t readU16(const std::vector<unsigned char>& d, std::size_t& pos) {
    const uint16_t v = YSE::MIDI::ReadU16BE(d.data() + pos);
    pos += 2;
    return v;
  }

  uint32_t readU32(const std::vector<unsigned char>& d, std::size_t& pos) {
    const uint32_t v = YSE::MIDI::ReadU32BE(d.data() + pos);
    pos += 4;
    return v;
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
  playheadSec = 0.0;
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
  // a fixed tick rate independent of tempo. Times are kept in SECONDS — never
  // samples — so the parsed events stay valid across a system::close()/init()
  // cycle at a different device rate (issue #637); advance() converts against
  // the live SAMPLERATE.
  const bool smpte = (division & 0x8000) != 0;
  double smpteSecondsPerTick = 0.0;
  uint32_t ppqn = 96;
  if (smpte) {
    const int framesPerSecond = -static_cast<int8_t>(division >> 8);
    const int ticksPerFrame = division & 0xFF;
    const double ticksPerSecond = static_cast<double>(framesPerSecond) * ticksPerFrame;
    if (ticksPerSecond > 0.0) smpteSecondsPerTick = 1.0 / ticksPerSecond;
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
      // Bounded by the whole buffer rather than by trackEnd, as before the
      // primitives moved: a delta-time that runs past the chunk boundary is
      // caught by the `pos >= trackEnd` test right here.
      if (!ReadVarLen(data.data(), pos, data.size(), delta) || pos >= trackEnd) {
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

      if (statusByte == META_PREFIX) {
        // Meta event: FF type len data...
        if (pos >= trackEnd) return false;
        const unsigned char metaType = data[pos++];
        uint32_t metaLen = 0;
        if (!ReadVarLen(data.data(), pos, data.size(), metaLen) || pos + metaLen > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI meta event: " + fileName);
          return false;
        }
        if (metaType == META_TEMPO && metaLen == 3) { // set tempo (us per quarter)
          const uint32_t us = (static_cast<uint32_t>(data[pos]) << 16) |
                              (static_cast<uint32_t>(data[pos + 1]) << 8) |
                              static_cast<uint32_t>(data[pos + 2]);
          tempo.push_back({tick, us, 0.0});
        }
        pos += metaLen;
        runningStatus = 0; // meta cancels running status
      } else if (statusByte == SYSEX_BEGIN || statusByte == SYSEX_ESCAPE) {
        // SysEx (or escape): F0/F7 len data... — skipped.
        uint32_t sysexLen = 0;
        if (!ReadVarLen(data.data(), pos, data.size(), sysexLen) || pos + sysexLen > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Malformed MIDI sysex event: " + fileName);
          return false;
        }
        pos += sysexLen;
        runningStatus = 0; // sysex cancels running status
      } else {
        // Channel-voice message.
        runningStatus = statusByte;
        const std::size_t nBytes = ChannelDataBytes(statusByte);
        if (pos + nBytes > trackEnd) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "Truncated MIDI channel message: " + fileName);
          return false;
        }
        const unsigned char d1 = data[pos];
        const unsigned char d2 = nBytes == 2 ? data[pos + 1] : 0;
        pos += nBytes;
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

  auto tickToSeconds = [&](uint64_t targetTick) -> double {
    if (smpte) return static_cast<double>(targetTick) * smpteSecondsPerTick;
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
    return us / 1'000'000.0;
  };

  midiEvents.reserve(raw.size());
  for (const RawEvent& e : raw)
    midiEvents.push_back({tickToSeconds(e.tick), e.status, e.data1, e.data2});

  // Stable sort so simultaneous events keep their in-file (track) order.
  std::stable_sort(
      midiEvents.begin(), midiEvents.end(),
      [](const fileEvent& a, const fileEvent& b) { return a.timeSeconds < b.timeSeconds; });

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
    playheadSec = 0.0;
    nextEvent = 0;
    intent.store(SS_STOPPED, std::memory_order_release);
    return;
  default: // SS_STOPPED / SS_PAUSED — idle
    return;
  }

  if (!hasFile || numSamples <= 0) return;

  // Convert this block's span to seconds against the live SAMPLERATE (issue
  // #637): the parsed timestamps are rate-independent seconds, so playback
  // timing follows whatever rate the current session negotiated. One divide
  // per block plus one compare per pending event — allocation-free.
  const double blockEndSec =
      playheadSec + static_cast<double>(numSamples) / static_cast<double>(SAMPLERATE);
  while (nextEvent < midiEvents.size() && midiEvents[nextEvent].timeSeconds < blockEndSec) {
    dispatchEvent(midiEvents[nextEvent]);
    ++nextEvent;
  }
  playheadSec = blockEndSec;

  if (nextEvent >= midiEvents.size()) {
    // One-shot playback: release held notes and rewind to the start.
    allNotesOffToSynths();
    playheadSec = 0.0;
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
