// `.midiflush` (issue #537). See mMidiFlush.h for the design; this file is the
// wire format, the tracking state machine and the flush.
//
// No platform guard, deliberately — see the header.
#include "mMidiFlush.h"

#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "pMidiByteList.h"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format. Spelled here rather than shared with mMidiCodec.cpp for
  // the reason that file gives for spelling it rather than sharing it with
  // mMidiIn.cpp: seven constants that have not changed since 1983 are not worth
  // a header binding two translation units together.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kNoteOff = 0x80;
  constexpr unsigned char kNoteOn = 0x90;
  constexpr unsigned char kProgramChange = 0xC0;
  constexpr unsigned char kChannelPressure = 0xD0;

  constexpr unsigned char kFirstStatus = 0x80;
  constexpr unsigned char kSysExStart = 0xF0;
  constexpr unsigned char kSysExEnd = 0xF7;
  constexpr unsigned char kFirstSystem = 0xF0;
  constexpr unsigned char kFirstRealTime = 0xF8;

  constexpr int kByteMax = 255;

  // How many data bytes a channel-voice message of this status carries.
  int VoiceDataBytes(unsigned char status) {
    const unsigned char kind = status & kStatusMask;
    return (kind == kProgramChange || kind == kChannelPressure) ? 1 : 2;
  }

  // How many data bytes a system-common message carries. 0xF4 and 0xF5 are
  // undefined and read as carrying none, which is the reading that lets the
  // decoder recover at the next status byte rather than swallowing it.
  int SystemDataBytes(unsigned char status) {
    switch (status) {
    case 0xF1:
      return 1; // MIDI time code quarter frame
    case 0xF2:
      return 2; // song position pointer
    case 0xF3:
      return 1; // song select
    default:
      return 0; // tune request, end of exclusive, and the undefined pair
    }
  }

  // Appends one number to a list being built, with the separator a list needs
  // between its atoms and none in front of its first. Allocation-free as long
  // as the caller reserved the string, which the constructor does; `WriteInt`
  // is the patcher's own decimal writer, which neither allocates nor reads
  // locale state.
  void AppendNumber(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    if (!out.empty()) out.push_back(' ');
    out.append(digits, written);
  }

} // namespace

#define className mMidiFlush

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Flush);
  REG_INT_IN(StreamInt);
  REG_FLOAT_IN(StreamFloat);
  REG_LIST_IN(StreamList);

  ADD_OUT_ANY;

  // The object's whole allocation, taken on the control thread before it is
  // wired: a note-off is three numbers, and from here on the flush only ever
  // writes into storage that already exists.
  scratch.reserve(static_cast<std::size_t>(3) * (FORMAT_INT_WIDTH + 1));

  ADD_DESCRIPTION(
      "Releases every note left hanging — Max's 'midiflush' (issue #537), and the safety valve a "
      "MIDI patch needs. Put it in the stream, between the senders and '.midiout' or straight "
      "after '.midiin': everything that arrives at the inlet goes out of the outlet untouched, and "
      "on the way through the object remembers which notes the bytes left sounding. A bang then "
      "sends one note-off per sounding note, on exactly the channel it was played on, and empties "
      "the set — so a patch stopped mid-phrase does not leave the device holding a chord until "
      "someone power-cycles it. Nothing else in the patcher can do this: the sender objects are "
      "stateless formatters that do not know what they have sent, and '.midiout''s 'allnotesoff' "
      "is controller 123, which many devices ignore and which reaches only the one port that "
      "object holds. The stream is decoded properly rather than searched for status bytes, because "
      "a note-on may arrive with no status byte in front of it at all (running status, which is "
      "how a chord is usually sent), a timing clock may land between its two data bytes, and a "
      "system-exclusive dump is full of bytes that would read as notes if they were not inside a "
      "dump — the same three rules '.midiparse' keeps. Both spellings of a release clear a note, a "
      "note-off message and a note-on with velocity 0, so a device that uses either is not flushed "
      "for notes it has already let go. Both of the patcher's spellings of a byte list are read, "
      "the numeric one ('144 60 100', what '.midiformat', '.sxformat' and '.seq' send) and the "
      "binary one (what '.noteon' and the older senders build), and a bare int is a byte too, "
      "which is the shape '.midiin' emits. The note-offs it generates go out as numeric lists — "
      "'128 60 0' — the only spelling that survives the 0 byte every one of them ends with. What "
      "is sounding is a fixed bitmap of 16 channels by 128 pitches allocated with the object, so "
      "neither the pass-through nor the flush allocates, locks or blocks on the audio thread. Like "
      "'.midiparse' it opens no device and needs none, so it works on every platform. It does not "
      "fire by itself when the patcher is torn down — bang it before the patch goes away.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "midi",
            "The MIDI stream, and the flush. Bytes arrive as a numeric list ('144 60 100', what "
            "'.midiformat' and '.seq' send), as a binary list (what '.noteon' and the older "
            "senders build), or as an int per byte (what '.midiin' emits); all three pass straight "
            "out of the outlet and are read for the notes they turn on and off. A float is read as "
            "an int, a value outside 0-255 is not a MIDI byte and is ignored rather than clamped, "
            "and a numeric list carrying one is dropped whole rather than sent in part. A bang "
            "here is the flush: a note-off for every note still sounding.",
            "0-255");
  OUTLET_DOC(0, "midi",
             "The stream, unaltered and in the spelling it arrived in, plus the note-offs a bang "
             "generates. Those go out as numeric byte lists, '128 60 0' being middle C released on "
             "channel 1, in channel then pitch order — feed the outlet to '.midiout' to reach "
             "hardware, or to '.midiparse' to read what went past.",
             "0-255");
}

INT_IN(StreamInt) {
  (void)inlet;
  // Not a MIDI byte. Ignored rather than clamped, which is `.midiparse`'s
  // reading: clamping would turn a stray number into a status byte and
  // desynchronise the stream that follows it.
  if (value < 0 || value > kByteMax) return;
  if (!Enter()) return;
  Byte((unsigned char)value);
  outputs[0].SendInt(value, thread);
  Leave();
}

FLOAT_IN(StreamFloat) {
  StreamInt((int)value, inlet, thread);
}

LIST_IN(StreamList) {
  (void)inlet;
  if (!Enter()) return;

  // On the stack rather than in a member: this handler runs on whichever thread
  // sent the message, and the guard above already keeps them apart, but a fixed
  // array costs nothing and keeps the object small. Nothing here allocates.
  unsigned char bytes[MIDI_BYTE_LIST_MAX];
  int count = 0;

  switch (ReadMidiByteList(value, bytes, MIDI_BYTE_LIST_MAX, count)) {
  case midiByteList::numeric:
    for (int i = 0; i < count; i++)
      Byte(bytes[i]);
    break;
  case midiByteList::characters:
    // The binary spelling: the characters are already the bytes.
    for (std::size_t i = 0; i < value.size(); i++)
      Byte((unsigned char)value[i]);
    break;
  case midiByteList::refused:
    // A numeric list with a number that is not a byte in it, or one longer than
    // the buffer. Not a MIDI message: dropped whole rather than passed on in
    // part, which is `.midiout`'s reading and the safer one at a device.
    dropped.fetch_add(1, std::memory_order_relaxed);
    Leave();
    return;
  }

  // Passed on whole and in the spelling it arrived in, after the tracking, so
  // that the object's own idea of what is sounding is settled before anything
  // downstream can act on the message.
  outputs[0].SendList(value, thread);
  Leave();
}

BANG_IN(Flush) {
  (void)inlet;
  if (!Enter()) return;

  for (int channel = 0; channel < CHANNELS; channel++) {
    for (int word = 0; word < WORDS; word++) {
      const std::uint32_t bits = held[channel][word];
      if (bits == 0) continue;
      // Cleared before anything is sent, so an outlet wired somewhere that
      // reads this object back sees a set that is already emptied of what is
      // on its way out.
      held[channel][word] = 0;
      for (int bit = 0; bit < 32; bit++) {
        if ((bits & (1u << bit)) == 0) continue;
        heldCount--;
        Emit(channel, (word * 32) + bit, thread);
      }
    }
  }

  Leave();
}

void mMidiFlush::Byte(unsigned char value) {
  // A real-time message is one byte and may appear anywhere at all, including
  // between the bytes of another message. It disturbs neither running status
  // nor a dump in progress, which is the rule that lets a clock pass through a
  // chord without breaking it.
  if (value >= kFirstRealTime) return;

  if (value >= kFirstStatus) {
    // Any status byte ends a system-exclusive dump, whether or not the device
    // bothered to send an EOX first. Hardware does exactly this when it is
    // interrupted, and a decoder that waited for the EOX would swallow every
    // message after it — and with them every note-on inside.
    if (inSysEx) {
      inSysEx = false;
      if (value == kSysExEnd) return;
    }

    systemStatus = 0;
    systemNeeded = 0;
    dataCount = 0;

    if (value >= kFirstSystem) {
      // Every system message clears running status; real time does not, which
      // is handled above.
      runningStatus = 0;
      if (value == kSysExStart) {
        inSysEx = true;
        return;
      }
      const int needed = SystemDataBytes(value);
      if (needed > 0) {
        systemStatus = value;
        systemNeeded = needed;
      }
      return;
    }

    runningStatus = value;
    return;
  }

  // A data byte.
  if (inSysEx) return;

  if (systemStatus != 0) {
    // Counted through rather than read: the point of following a system message
    // at all is that its data bytes are not notes.
    systemNeeded--;
    if (systemNeeded <= 0) systemStatus = 0;
    return;
  }

  // No status to read it under: a stream joined part-way through, or a device
  // that sent rubbish. Dropped rather than guessed at.
  if (runningStatus == 0) return;

  data[dataCount] = value;
  dataCount++;
  if (dataCount < VoiceDataBytes(runningStatus)) return;

  // Running status: the count goes back to zero and the status stays, so the
  // next pair of data bytes is another message of the same kind.
  dataCount = 0;
  Voice(runningStatus);
}

void mMidiFlush::Voice(unsigned char status) {
  const unsigned char kind = status & kStatusMask;
  if (kind != kNoteOn && kind != kNoteOff) return;

  const int channel = (int)(status & kChannelMask);
  const int pitch = (int)(data[0] & kDataMask);
  // Both spellings of a release: a note-off message, and a note-on whose
  // velocity is 0. Folding them together here is not cosmetic — an object that
  // understood only one would flush notes the device had already let go.
  const bool on = (kind == kNoteOn) && ((data[1] & kDataMask) != 0);

  Mark(channel, pitch, on);
}

void mMidiFlush::Mark(int channel, int pitch, bool on) {
  std::uint32_t& word = held[channel][pitch / 32];
  const std::uint32_t bit = 1u << (pitch % 32);

  if (on) {
    if ((word & bit) == 0) {
      word |= bit;
      heldCount++;
    }
    return;
  }

  if ((word & bit) != 0) {
    word &= ~bit;
    heldCount--;
  }
}

void mMidiFlush::Emit(int channel, int pitch, YSE::THREAD thread) {
  scratch.clear();
  AppendNumber(scratch, (int)(kNoteOff | (unsigned char)channel));
  AppendNumber(scratch, pitch);
  // Release velocity 0: the conventional "no velocity information", and what
  // the devices that ignore release velocity — nearly all of them — expect.
  AppendNumber(scratch, 0);
  outputs[0].SendList(scratch, thread);
}

bool mMidiFlush::IsHeld(int channel, int pitch) const {
  // 1-16 from outside, as everywhere a patcher object reports a channel.
  if (channel < 1 || channel > CHANNELS) return false;
  if (pitch < 0 || pitch >= PITCHES) return false;
  return (held[channel - 1][pitch / 32] & (1u << (pitch % 32))) != 0;
}

bool mMidiFlush::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired this object's outlet
    // back into its inlet. Counted rather than spun on: this is a path the
    // audio callback takes, and neither the decoder nor the flush is
    // re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mMidiFlush::Leave() {
  busy.store(false, std::memory_order_release);
}

#undef className
