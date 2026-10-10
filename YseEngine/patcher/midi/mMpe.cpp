// The MPE family (issue #535) — `.mpeconfig`, `.mpeformat` and `.mpeparse`.
// See mMpe.h for the design; this file is the wire format and the one state
// machine.
//
// No platform guard, deliberately — see the header.
#include "mMpe.h"

#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format, spelled locally as mMidiIn.cpp, mMidiCodec.cpp and
  // mMidiRpnOut.cpp all do. Sharing constants that have not moved since 1983
  // across files with different platform guards would cost more than it saves.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kNoteOff = 0x80;
  constexpr unsigned char kNoteOn = 0x90;
  constexpr unsigned char kControlChange = 0xB0;
  constexpr unsigned char kProgramChange = 0xC0;
  constexpr unsigned char kChannelPressure = 0xD0;
  constexpr unsigned char kPitchBend = 0xE0;

  constexpr unsigned char kFirstStatus = 0x80;
  constexpr unsigned char kFirstSystem = 0xF0;
  constexpr unsigned char kSysExStart = 0xF0;
  constexpr unsigned char kSysExEnd = 0xF7;
  constexpr unsigned char kFirstRealTime = 0xF8;

  // The three controllers an MPE Configuration Message is made of: the pair
  // that selects a registered parameter number, and the one that writes the
  // value into it.
  constexpr int kRpnSelectLsb = 100;
  constexpr int kRpnSelectMsb = 101;
  constexpr int kDataEntryMsb = 6;

  constexpr int kByteMax = 255;
  constexpr int kMax7Bit = 127;
  constexpr int kMax14Bit = 16383;

  // How many numbers one outgoing message ever needs. Three is the widest MIDI
  // channel-voice message, and keeping the buffer this size is what lets every
  // outgoing list be built on the stack.
  constexpr int kMessageBytes = 3;

  int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
  }

  int Clamp7(int value) {
    return Clamp(value, 0, kMax7Bit);
  }

  // How many data bytes a channel-voice message of this status carries. Program
  // change and channel pressure take one; everything else takes two.
  int VoiceDataBytes(unsigned char status) {
    const unsigned char kind = status & kStatusMask;
    return (kind == kProgramChange || kind == kChannelPressure) ? 1 : 2;
  }

  // How many data bytes a system-common message carries. 0xF4 and 0xF5 are
  // undefined and read as carrying none, which is the reading that lets the
  // framing recover at the next status byte rather than swallowing it.
  int SystemDataBytes(unsigned char status) {
    switch (status) {
    case 0xF1:
      return 1; // MIDI time code quarter frame
    case 0xF2:
      return 2; // song position pointer
    case 0xF3:
      return 1; // song select
    default:
      return 0; // tune request, and the undefined pair
    }
  }

  constexpr char kZoneParamDoc[] =
      "0 for the lower zone (master channel 1, members counting up from 2) or 1 for the upper zone "
      "(master channel 16, members counting down from 15). Anything other than 1 means the lower "
      "zone.";

} // namespace

// ─── .mpeconfig ─────────────────────────────────────────────────────────────

#define className mMpeConfig

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntMembers);

  ADD_OUT_LIST;

  ADD_PARAM(zone);

  zone = MPE_ZONE_LOWER;
  members = MPE_MEMBERS_MAX;

  ADD_DESCRIPTION("Builds the MPE configuration message that tells a device how many member "
                  "channels an MPE zone uses. A value on the inlet sends it.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "members",
            "Number of member channels; sends the message. 0 switches MPE off on the device.",
            "0-15");
  OUTLET_DOC(0, "midi",
             "Three control-change messages on the zone's master channel, as byte lists for "
             ".midiout.",
             "0-255");
  PARAM_DOC("zone", "0", kZoneParamDoc, "0-1");
}

INT_IN(SetIntMembers) {
  (void)inlet;
  (void)thread;
  members = MpeClampMembers(value);
}

CALC() {
  int message[kMessageBytes];
  message[0] = kControlChange + MpeMasterNibble(MpeClampZone(zone));

  // The specification's own byte order: the parameter number's fine byte
  // first — controller 100 carrying 6 — then its coarse byte on controller 101.
  // See the header for why this is the reverse of `.rpnout`'s.
  message[1] = kRpnSelectLsb;
  message[2] = MPE_CONFIG_RPN;
  outputs[0].SendList(FormatIntList(message, kMessageBytes), thread);

  message[1] = kRpnSelectMsb;
  message[2] = 0;
  outputs[0].SendList(FormatIntList(message, kMessageBytes), thread);

  // Then the value the whole message exists to carry.
  message[1] = kDataEntryMsb;
  message[2] = MpeClampMembers(members);
  outputs[0].SendList(FormatIntList(message, kMessageBytes), thread);
}

#undef className

// ─── .mpeformat ─────────────────────────────────────────────────────────────

#define className mMpeFormat

CONSTRUCT() {
  // Laid out to mirror `.mpeparse`'s outlets, so the pair wires straight
  // across — the discipline `.midiparse` and `.midiformat` already keep.
  ADD_IN_0; // note
  REG_INT_IN(FormatInt);
  REG_FLOAT_IN(FormatFloat);
  REG_LIST_IN(FormatList);

  // bend, pressure, slide, channel. A list means something only on the note
  // inlet, which takes two numbers; registering it nowhere else keeps
  // GetAcceptedTypes() reporting the real contract — the `.zl` / `.pack`
  // discipline.
  for (int i = 1; i < 5; i++) {
    inputs.emplace_back(this, false, i);
    REG_INT_IN(FormatInt);
    REG_FLOAT_IN(FormatFloat);
  }

  ADD_OUT_LIST;

  ADD_DESCRIPTION("Builds MPE messages: notes, per-note pitch bend, pressure and slide, on the "
                  "member channel set by the right inlet. The inverse of .mpeparse.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "note",
            "\"pitch velocity\"; a bare pitch sends velocity 0, a release. Sends a note-on.",
            "0-127 0-127");
  INLET_DOC(1, "bend", "Per-note pitch bend, 8192 at rest, at the full 14 bits.", "0-16383");
  INLET_DOC(2, "pressure", "Per-note pressure, sent as channel pressure.", "0-127");
  INLET_DOC(3, "slide", "Per-note slide, sent as controller 74.", "0-127");
  INLET_DOC(4, "channel", "Member channel for the messages that follow. Stores only; starts at 1.",
            "1-16");
  OUTLET_DOC(0, "midi",
             "One message per list, such as 146 60 100: a note-on for middle C on channel 3.",
             "0-255");
}

INT_IN(FormatInt) {
  switch (inlet) {
  case 0:
    // Max's rule for a two-number inlet: a bare number is the first of the
    // pair, the second being 0 — here a pitch and a release.
    Note(value, 0, thread);
    break;
  case 1: {
    const int bend = Clamp(value, 0, kMax14Bit);
    // The wire sends the fine byte first.
    Emit(kPitchBend, bend & kMax7Bit, (bend >> 7) & kMax7Bit, thread);
    break;
  }
  case 2:
    Emit(kChannelPressure, Clamp7(value), thread);
    break;
  case 3:
    Emit(kControlChange, MPE_SLIDE_CONTROLLER, Clamp7(value), thread);
    break;
  case 4:
    channel = Clamp(value, 1, 16);
    break;
  default:
    break;
  }
}

FLOAT_IN(FormatFloat) {
  FormatInt((int)value, inlet, thread);
}

LIST_IN(FormatList) {
  (void)inlet;
  std::size_t cursor = 0;
  int pitch = 0;
  int velocity = 0;
  if (!ReadIntArgAt(value, cursor, pitch)) return;
  if (!ReadIntArgAt(value, cursor, velocity)) velocity = 0;
  Note(pitch, velocity, thread);
}

void mMpeFormat::Note(int pitch, int velocity, YSE::THREAD thread) {
  Emit(kNoteOn, Clamp7(pitch), Clamp7(velocity), thread);
}

void mMpeFormat::Emit(int status, int first, int second, YSE::THREAD thread) {
  int message[kMessageBytes];
  message[0] = status + (channel - 1);
  message[1] = first;
  message[2] = second;
  outputs[0].SendList(FormatIntList(message, kMessageBytes), thread);
}

void mMpeFormat::Emit(int status, int first, YSE::THREAD thread) {
  int message[2];
  message[0] = status + (channel - 1);
  message[1] = first;
  outputs[0].SendList(FormatIntList(message, 2), thread);
}

#undef className

// ─── .mpeparse ──────────────────────────────────────────────────────────────

#define className mMpeParse

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(ParseInt);
  REG_FLOAT_IN(ParseFloat);
  REG_LIST_IN(ParseList);

  ADD_OUT_LIST; // 0 note
  ADD_OUT_INT; // 1 bend
  ADD_OUT_INT; // 2 pressure
  ADD_OUT_INT; // 3 slide
  ADD_OUT_INT; // 4 channel
  ADD_OUT_INT; // 5 role

  ADD_PARAM(zone);
  ADD_PARAM(members);

  zone = MPE_ZONE_LOWER;
  members = MPE_MEMBERS_MAX;

  ADD_DESCRIPTION("Reads an MPE MIDI stream and reports notes, per-note pitch bend, pressure and "
                  "slide, each with its channel and the channel's role in the zone.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "midi",
            "MIDI bytes, one int at a time or as a list of byte values. A value outside 0-255 is "
            "ignored.",
            "0-255");
  OUTLET_DOC(0, "note", "Note as \"pitch velocity\"; a release reports velocity 0.", "0-127 0-127");
  OUTLET_DOC(1, "bend", "Pitch bend at the full 14 bits, 8192 at rest.", "0-16383");
  OUTLET_DOC(2, "pressure", "Channel pressure: on a member channel, that note's pressure.",
             "0-127");
  OUTLET_DOC(3, "slide", "Controller 74.", "0-127");
  OUTLET_DOC(4, "channel", "The channel the message arrived on: in MPE, the note it belongs to.",
             "1-16");
  OUTLET_DOC(5, "role", "The channel's role: 2 master, 1 member, 0 outside the zone. Sent first.",
             "0-2");
  PARAM_DOC("zone", "0", kZoneParamDoc, "0-1");
  PARAM_DOC("members", "15",
            "Member channels to assume until a configuration message in the stream changes it.",
            "0-15");
}

INT_IN(ParseInt) {
  (void)inlet;
  // Not a MIDI byte. Ignored rather than clamped: clamping would turn a stray
  // number into a status byte and desynchronise the stream that follows it.
  if (value < 0 || value > kByteMax) return;
  if (!Enter()) return;
  Byte((unsigned char)value, thread);
  Leave();
}

FLOAT_IN(ParseFloat) {
  ParseInt((int)value, inlet, thread);
}

LIST_IN(ParseList) {
  (void)inlet;
  if (!Enter()) return;
  // The guard is taken once for the whole list rather than per byte, so the
  // bytes of one message cannot be interleaved with another thread's.
  std::size_t cursor = 0;
  int number = 0;
  while (ReadIntArgAt(value, cursor, number)) {
    if (number < 0 || number > kByteMax) continue;
    Byte((unsigned char)number, thread);
  }
  Leave();
}

void mMpeParse::Byte(unsigned char value, YSE::THREAD thread) {
  // A real-time message is one byte and may appear anywhere at all, including
  // between the bytes of another message. It disturbs neither running status
  // nor a dump in progress, and this object has nothing to say about it.
  if (value >= kFirstRealTime) return;

  if (value >= kFirstStatus) {
    // Any status byte ends a system-exclusive dump, whether or not the device
    // bothered to send an EOX first. Hardware does exactly this when it is
    // interrupted, and a parser that waited for the EOX would swallow every
    // message after it.
    if (inSysEx) {
      inSysEx = false;
      if (value == kSysExEnd) return;
    }

    dataCount = 0;
    systemNeeded = 0;

    if (value >= kFirstSystem) {
      // Every system-common message clears running status. Real time does not,
      // which is handled above — that distinction is the whole reason a clock
      // can pass through a chord without breaking it.
      runningStatus = 0;
      if (value == kSysExStart)
        inSysEx = true;
      else
        systemNeeded = SystemDataBytes(value);
      return;
    }

    runningStatus = value;
    return;
  }

  // A data byte.
  if (inSysEx) return;

  // Swallowed rather than decoded: song position and MIDI time code are
  // protocol this object has no business with, and only their *length* matters
  // here, so that their data bytes cannot be read as a voice message's.
  if (systemNeeded > 0) {
    systemNeeded--;
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
  Voice(runningStatus, thread);
}

void mMpeParse::Voice(unsigned char status, YSE::THREAD thread) {
  const int nibble = (int)(status & kChannelMask);
  const int first = (int)(data[0] & kDataMask);
  const int second = (int)(data[1] & kDataMask);

  switch (status & kStatusMask) {
  case kNoteOn:
    ReportNote(nibble, first, second, thread);
    break;
  // Max's rule, shared with `.notein` and `.midiparse`: a note-off reports
  // velocity 0 rather than the release velocity it carries, so the two
  // spellings of a release are one shape downstream.
  case kNoteOff:
    ReportNote(nibble, first, 0, thread);
    break;
  case kChannelPressure:
    Report(nibble, 2, first, thread);
    break;
  // The wire sends the fine byte first, and this object keeps both — the whole
  // point of a per-note bend at a 48-semitone range.
  case kPitchBend:
    Report(nibble, 1, (second << 7) | first, thread);
    break;
  case kControlChange:
    // The configuration message is the one control change this object decodes
    // for itself, and it is consumed rather than reported.
    if (Config(nibble, first, second)) break;
    if (first == MPE_SLIDE_CONTROLLER) Report(nibble, 3, second, thread);
    break;
  default:
    break;
  }
}

bool mMpeParse::Config(int nibble, int controller, int value) {
  // Only the watched zone's master channel carries configuration. A member
  // channel that happened to send controller 6 is a device writing to some
  // other parameter, and reading it as a zone resize would silently rewire the
  // patch's idea of which channels are notes.
  if (nibble != MpeMasterNibble(MpeClampZone(zone.load()))) return false;

  switch (controller) {
  case kRpnSelectMsb:
    rpnMsb = (unsigned char)value;
    return true;
  case kRpnSelectLsb:
    rpnLsb = (unsigned char)value;
    return true;
  case kDataEntryMsb:
    if (rpnMsb != 0 || rpnLsb != MPE_CONFIG_RPN) return false;
    members = MpeClampMembers(value);
    return true;
  default:
    return false;
  }
}

void mMpeParse::Report(int nibble, int outlet, int value, YSE::THREAD thread) {
  outputs[5].SendInt(MpeRole(MpeClampZone(zone.load()), Members(), nibble), thread);
  outputs[4].SendInt(nibble + 1, thread);
  outputs[(std::size_t)outlet].SendInt(value, thread);
}

void mMpeParse::ReportNote(int nibble, int pitch, int velocity, YSE::THREAD thread) {
  outputs[5].SendInt(MpeRole(MpeClampZone(zone.load()), Members(), nibble), thread);
  outputs[4].SendInt(nibble + 1, thread);

  int note[2];
  note[0] = pitch;
  note[1] = velocity;
  outputs[0].SendList(FormatIntList(note, 2), thread);
}

bool mMpeParse::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired this object's outlet
    // back into its inlet. Counted rather than spun on: this is a path the
    // audio callback takes, and the state machine is not re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mMpeParse::Leave() {
  busy.store(false, std::memory_order_release);
}

#undef className
