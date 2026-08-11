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
      "Which MPE zone this object belongs to: 0 for the lower zone, whose master is channel 1 and "
      "whose member channels count up from 2, or 1 for the upper zone, whose master is channel 16 "
      "and whose members count down from 15. The two grow away from each other so that two "
      "instruments can share one cable. 0 is the default, is the specification's recommended "
      "power-on state, and is what a controller in its factory state uses; anything other than 1 "
      "is "
      "read as the lower zone. MPE defines these two zones and no others — Max's seven-zone "
      "'masterchan' arrangement is Max's own, and hardware implements the specification.";

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

  ADD_DESCRIPTION(
      "Configures an MPE zone on a controller or synthesiser — Max's 'mpeconfig' (issue #535). MPE "
      "gives every sounding note a MIDI channel of its own, so that pitch bend, pressure and "
      "controller 74 stop being per-instrument and become per-note; that only works if the two "
      "ends "
      "agree on which channels are notes and which one carries the instrument as a whole, and the "
      "MPE Configuration Message is that agreement. It is the one message in the whole of MPE that "
      "is not ordinary MIDI used in a particular way: registered parameter number '00 06', so "
      "three "
      "control changes — controllers 100 and 101 select the parameter and controller 6 carries the "
      "number of member channels — sent on the zone's master channel, which is channel 1 for the "
      "lower zone and channel 16 for the upper. A count of 0 turns the zone off, which is the "
      "specification's own way of saying 'go back to plain MIDI' and is what a patch sends when it "
      "hands the controller to something that is not MPE-aware, so it is a real value rather than "
      "a "
      "degenerate one. Counts are clamped into 0-15 rather than refused, as everywhere in this "
      "family. The three messages leave as three separate lists because '.midiout' sends each list "
      "it receives as a single MIDI message, so nine bytes together would arrive as one malformed "
      "message rather than three good ones; the fine byte of the parameter number goes first, "
      "which "
      "is the reverse of '.rpnout''s order and is the literal byte sequence the MPE specification "
      "prints for this message, so that a device pattern-matching the configuration message rather "
      "than running a general parameter-number state machine still recognises it. The Data Entry "
      "LSB that would follow on controller 38 is not sent, the configuration message's fine byte "
      "being explicitly unused. Worth knowing at the far end: receiving this message requires a "
      "device to set its master channel's pitch-bend range to two semitones and every member "
      "channel's to 48, and to stop ongoing notes and reset controllers on every channel entering "
      "or leaving the zone — which is why '.mpeformat' takes a fourteen-bit bend. The output is a "
      "numeric byte list, the shape '.midiformat' and '.sxformat' send and '.midiout' reads. "
      "Unlike "
      "Max, which allows up to seven zones placed anywhere among the sixteen channels, this object "
      "implements the two zones MPE 1.0 defines, because what a patch gains here is talking to "
      "real "
      "hardware. It opens no device and needs none, so it runs on every platform.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(
      0, "members",
      "How many member channels the zone is to have, 0-15 (also fires the three messages). 15 "
      "is the usual answer and the default — the whole of the rest of the cable, which is "
      "what a controller playing on its own wants. A smaller number leaves room for a second "
      "zone or for plain-MIDI traffic beside it. 0 switches the zone off, which is a real "
      "instruction rather than a refusal.",
      "0-15");
  OUTLET_DOC(0, "midi",
             "Three encoded MIDI Control Change messages, one list each and in order: controller "
             "100 and controller 101 selecting registered parameter '00 06', then controller 6 "
             "carrying the member channel count. Three lists rather than one, because '.midiout' "
             "sends each list it receives as a single MIDI message. Wire it straight to a "
             "'.midiout' pointed at the controller.",
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

  ADD_DESCRIPTION(
      "Assembles MPE MIDI messages — Max's 'mpeformat' (issue #535), and the exact inverse of "
      "'.mpeparse', inlet for outlet, so the two wire straight across. MPE's expressive gestures "
      "are not new messages: they are ordinary note, pitch bend, channel pressure and controller "
      "74 "
      "messages sent on a channel that stands for one note rather than for a whole instrument. "
      "This "
      "is the box that addresses them — set the member channel on the right inlet once, then feed "
      "it notes, bends, pressures and slides, and each leaves as a complete MIDI message belonging "
      "to that note. Two readings differ from '.midiformat' and both are the point of the object. "
      "Bend is the full 14 bits, 0-16383 with 8192 at rest, rather than the coarse byte alone: "
      "receiving an MPE configuration message sets a device's per-note bend range to 48 semitones "
      "instead of 2, so a seven-bit bend would step in three quarters of a semitone and a glide "
      "would sound like a staircase. And pressure and slide are per-note here because the channel "
      "makes them so — channel pressure on a member channel is that one note's pressure, and "
      "controller 74 on it is that note's forward-back position. A release is a note-on with "
      "velocity 0, which is what '.midiformat' sends and what '.mpeparse' reports, so a note "
      "round-trips through the pair as the pair described it; a real note-off message is "
      "'.noteoff''s and '.xnoteout''s. Values are clamped into the ranges MIDI has rather than "
      "refused. Each message leaves as one numeric byte list, the shape '.midiout' reads. Where "
      "Max "
      "gives this object one inlet per member channel and emits 'mpeevent' messages for 'poly~' "
      "and "
      "'vst~', this patcher has neither and an object whose inlet count is a creation argument "
      "would have no stable inlet numbering, so the member channel is a single cold inlet as it is "
      "on '.midiformat'. Use '.mpeconfig' to tell the receiving device how many member channels to "
      "expect. The object opens no device and needs none, so it runs on every platform, and it "
      "allocates nothing on a message path.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(
      0, "note",
      "The list 'pitch velocity', or a bare pitch with velocity 0. Velocity 0 is a release, "
      "which is how '.mpeparse' reports one, so the pair round-trips; a real note-off message "
      "(status 128) is what '.noteoff' and '.xnoteout' send and is not produced here.",
      "0-127 0-127");
  INLET_DOC(1, "bend",
            "Per-note pitch bend, 0-16383 with 8192 at rest — all fourteen bits, not "
            "'.midiformat''s coarse byte. The whole of MPE's pitch expression rides on this inlet, "
            "and at the 48-semitone range a configured device uses, seven bits would not be enough "
            "to glide smoothly.",
            "0-16383");
  INLET_DOC(2, "pressure",
            "Per-note pressure, 0-127: how hard the one finger holding this note is pressing. Sent "
            "as channel pressure, which on a member channel belongs to that note alone.",
            "0-127");
  INLET_DOC(3, "slide",
            "Per-note slide, 0-127 — controller 74, MPE's third dimension, the forward-back "
            "position of a finger on a controller that has one. Also called timbre, or the Y axis.",
            "0-127");
  INLET_DOC(
      4, "channel",
      "The member channel every message is addressed to, 1-16. Cold: it stores and sends "
      "nothing, which is '.midiformat''s rule for the same inlet. One channel is one note, so "
      "a polyphonic patch moves this inlet between notes. Defaults to 1.",
      "1-16");
  OUTLET_DOC(
      0, "midi",
      "One complete MIDI message per hot inlet message, as a numeric byte list: '146 60 100' "
      "is a note-on for middle C on channel 3. Wire it to a '.midiout', or to a '.mpeparse' "
      "to read back what was built.",
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

  ADD_DESCRIPTION(
      "Interprets raw MPE MIDI data — Max's 'mpeparse' (issue #535). MPE gives every sounding note "
      "a MIDI channel of its own, so pitch bend, channel pressure and controller 74 — three "
      "ordinary per-channel messages — become per-note ones, and reading an MPE stream is a matter "
      "of knowing which channel a message arrived on and what that channel is doing. Bytes arrive "
      "at the inlet one at a time, as '.midiin' and '.seq' send them, or as a numeric list, as "
      "'.mpeformat' and '.midiformat' send them, and the four MPE gestures leave the four leftmost "
      "outlets, each preceded by the channel that carried it and by that channel's role in the "
      "zone. The role is the second half of the answer the channel gives: a message on the zone's "
      "master channel applies to every note at once, one on a member channel belongs to the single "
      "note sounding there, and one on a channel outside the zone belongs to neither — the other "
      "zone's traffic, or plain MIDI sharing the cable. All three are reported rather than the "
      "third being dropped, because a patch can filter but cannot recover what an object threw "
      "away. Bend is the full 14 bits, 0-16383 with 8192 at rest, which is what a device "
      "configured "
      "for MPE's 48-semitone per-note range needs and what '.xbendin' also reports. The object "
      "learns as it reads: the 'zone' and 'members' arguments say what to assume and default to "
      "the "
      "specification's recommended power-on state — the lower zone with all fifteen member "
      "channels "
      "— and an MPE Configuration Message seen on that zone's master channel updates the member "
      "count live, so a patch that sends '.mpeconfig' at one end reads the right roles at the "
      "other "
      "without being told twice. That message is consumed rather than reported: it is "
      "configuration, not expression. Program changes, other controllers, polyphonic key pressure "
      "and system exclusive are deliberately not decoded here — Max's object is its 'midiparse' "
      "plus four MPE outlets, while this patcher already has '.midiparse', and two decoders to "
      "keep "
      "in step is one too many; wire '.midiparse' to the same source and nothing in the stream is "
      "lost. Running status, interleaved real-time bytes and system-exclusive dumps are all "
      "tracked "
      "so that they cannot desynchronise the framing. The object opens no device and needs none, "
      "so "
      "it decodes a stream from a file, from '.seq' or from a patch on every platform.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(
      0, "midi",
      "Raw MIDI bytes: an int per byte, as '.midiin' and '.seq' send them, or a numeric list, "
      "as '.mpeformat' and '.midiformat' send them. Both go through the same state machine, "
      "so a message split across several messages decodes exactly as one arriving whole does. "
      "A float is read as an int; a value outside 0-255 is not a MIDI byte and is ignored "
      "rather than clamped, since clamping would turn a stray number into a status byte and "
      "desynchronise everything after it.",
      "0-255");
  OUTLET_DOC(0, "note",
             "Note-on and note-off as the list 'pitch velocity'. A release is reported as velocity "
             "0 whichever way the device spelled it — a note-off message, or a note-on with "
             "velocity 0 — so a patch tests for it once, with a '.sel 0' or a '.togedge'. On a "
             "member channel this is the note that channel now stands for.",
             "0-127 0-127");
  OUTLET_DOC(1, "bend",
             "Pitch bend, 0-16383 with 8192 at rest — all fourteen bits, not '.bendin''s coarse "
             "byte. On a member channel this bends the one note sounding there; on the master "
             "channel it bends the whole zone, and a receiver is required to combine the two.",
             "0-16383");
  OUTLET_DOC(
      2, "pressure",
      "Channel pressure, 0-127. On a member channel this is the pressure of the one finger "
      "holding that note — MPE's second dimension — rather than a single value for the whole "
      "keyboard, which is what makes it worth having a separate object for.",
      "0-127");
  OUTLET_DOC(3, "slide",
             "Controller 74, 0-127 — MPE's third dimension, the forward-back position of a finger. "
             "Also called timbre, or the Y axis. Every other controller belongs to '.midiparse'.",
             "0-127");
  OUTLET_DOC(4, "channel",
             "The channel the message arrived on, 1-16 rather than the 0-15 nibble on the wire. In "
             "MPE this is the note's identity — one channel is one note — so a patch routes on it "
             "to keep voices apart. Sent before the outlets to its left, so anything a value "
             "triggers downstream already knows whose note it is.",
             "1-16");
  OUTLET_DOC(
      5, "role",
      "What the channel is doing in the zone: 2 for the master channel, whose messages apply "
      "to every note at once; 1 for a member channel, whose messages belong to the one note "
      "sounding there; 0 for a channel taking no part in this zone, which is the other "
      "zone's traffic or plain MIDI sharing the cable. Sent first of all, before the channel "
      "outlet.",
      "0-2");
  PARAM_DOC("zone", "0", kZoneParamDoc, "0-1");
  PARAM_DOC("members", "15",
            "How many member channels to assume the zone has until a configuration message in the "
            "stream says otherwise, 0-15. 15 is the default and the specification's recommended "
            "power-on state — the whole of the rest of the cable. It decides only which channels "
            "the role outlet calls members; nothing is filtered out on the strength of it.",
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
