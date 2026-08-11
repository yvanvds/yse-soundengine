// `.midiparse` and `.midiformat` (issue #530). See mMidiCodec.h for the
// design; this file is the wire format and the two state machines.
//
// No platform guard, deliberately — see the header.
#include "mMidiCodec.h"

#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format. Spelled here rather than shared with mMidiIn.cpp on
  // purpose: that file is compiled only where a MIDI device backend exists and
  // this one is compiled everywhere, so a shared header would be the only way
  // to link them — for seven constants that have not changed since 1983.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kNoteOff = 0x80;
  constexpr unsigned char kNoteOn = 0x90;
  constexpr unsigned char kPolyPressure = 0xA0;
  constexpr unsigned char kControlChange = 0xB0;
  constexpr unsigned char kProgramChange = 0xC0;
  constexpr unsigned char kChannelPressure = 0xD0;
  constexpr unsigned char kPitchBend = 0xE0;

  constexpr unsigned char kFirstStatus = 0x80;
  constexpr unsigned char kSysExStart = 0xF0;
  constexpr unsigned char kSysExEnd = 0xF7;
  constexpr unsigned char kFirstSystem = 0xF0;
  constexpr unsigned char kFirstRealTime = 0xF8;

  constexpr int kByteMax = 255;

  // How many data bytes a channel-voice message of this status carries. Program
  // change and channel pressure take one; everything else takes two.
  int VoiceDataBytes(unsigned char status) {
    const unsigned char kind = status & kStatusMask;
    return (kind == kProgramChange || kind == kChannelPressure) ? 1 : 2;
  }

  // How many data bytes a system-common message carries. 0xF4 and 0xF5 are
  // undefined and read as carrying none, which is the reading that lets the
  // parser recover at the next status byte rather than swallowing it.
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

  // Appends one number to a list being built, with the separator a list needs
  // between its atoms and none in front of its first. Allocation-free as long
  // as the caller reserved the string, which both objects do in their
  // constructors; `WriteInt` is the patcher's own decimal writer, which neither
  // allocates nor reads locale state.
  void AppendNumber(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    if (!out.empty()) out.push_back(' ');
    out.append(digits, written);
  }

  int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
  }

  int Clamp7(int value) {
    return Clamp(value, 0, 127);
  }

} // namespace

// ─── .midiparse ─────────────────────────────────────────────────────────────

#define className mMidiParse

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(ParseInt);
  REG_FLOAT_IN(ParseFloat);
  REG_LIST_IN(ParseList);

  ADD_OUT_LIST; // 0 note
  ADD_OUT_LIST; // 1 polyphonic key pressure
  ADD_OUT_LIST; // 2 control change
  ADD_OUT_INT; // 3 program change
  ADD_OUT_INT; // 4 channel aftertouch
  ADD_OUT_INT; // 5 pitch bend
  ADD_OUT_INT; // 6 channel
  ADD_OUT_LIST; // 7 everything else

  // The object's whole allocation, taken on the control thread before it is
  // wired: from here on a byte arriving on the audio thread only ever appends
  // into storage that already exists.
  raw.reserve(static_cast<std::size_t>(RAW_CHUNK_BYTES) * (FORMAT_INT_WIDTH + 1));
  scratch.reserve(static_cast<std::size_t>(2) * (FORMAT_INT_WIDTH + 1));

  ADD_DESCRIPTION(
      "Interprets a raw MIDI byte stream into typed outlets — Max's 'midiparse' (issue #530), and "
      "what makes '.midiin' usable without seven separate objects. Bytes arrive in the inlet as "
      "ints (one at a time, the shape '.midiin' and '.seq' emit) or as a numeric list (the shape "
      "'.midiformat' emits), and each complete message leaves the outlet that belongs to its type: "
      "note, polyphonic key pressure and control change as two-number lists, program change, "
      "aftertouch and pitch bend as ints, the channel on its own outlet, and everything the "
      "decoding does not cover — system exclusive, song position, MIDI time code, the real-time "
      "clock — as a list of bytes on the rightmost. It knows the three things that make MIDI "
      "harder than it looks: running status, so a chord sent as one status byte followed by pairs "
      "of data bytes decodes as the notes it is; interleaving, so a timing clock arriving between "
      "two bytes of a note is reported at once and the note carries on where it left off; and "
      "messages that do not fit, so a system-exclusive dump longer than the buffer leaves in "
      "consecutive chunks rather than being truncated. The channel outlet fires before the typed "
      "one, as the '.notein' family's outlets do, so whatever a value triggers downstream already "
      "knows which channel it belongs to. Three readings are Max's and are shared with '.notein', "
      "'.pgmin' and '.bendin' so that a number means the same thing wherever a patch reads it: a "
      "note-off is reported as that pitch with velocity 0, which also folds together the two "
      "spellings hardware uses for a release; a program change is 1-128 rather than the wire's "
      "0-127, the numbering hardware displays; and pitch bend is the coarse byte alone, 0-127 "
      "centred at 64, with the full 14 bits left to '.xbendin'. The last two are lossy through a "
      "round trip back into '.midiformat', which is Max's behaviour rather than an oversight. "
      "Unlike the rest of this family the object opens no device and needs none, so it decodes a "
      "stream from a file, from '.seq' or from a patch on every platform.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "midi",
            "Raw MIDI bytes: an int per byte, as '.midiin' and '.seq' send them, or a numeric "
            "list, as '.midiformat' sends them. Both go through the same state machine, so a "
            "message split across several messages decodes exactly as one arriving whole does. A "
            "float is read as an int; a value outside 0-255 is not a MIDI byte and is ignored.",
            "0-255");
  OUTLET_DOC(0, "note",
             "Note-on and note-off as the list 'pitch velocity'. A release is reported as velocity "
             "0 whichever way the device spelled it — a note-off message, or a note-on with "
             "velocity 0 — so a patch tests for it once, with a '.sel 0' or a '.togedge'.",
             "0-127 0-127");
  OUTLET_DOC(1, "poly",
             "Polyphonic key pressure as the list 'pitch pressure': the pressure on one held key, "
             "which is what separates it from the aftertouch outlet's single value for the whole "
             "channel.",
             "0-127 0-127");
  OUTLET_DOC(2, "control",
             "Control change as the list 'controller value' — the knobs, faders, wheels and pedals "
             "of a control surface. Feed the list to a '.unpack' or a '.route' to fan one stream "
             "out by controller number.",
             "0-127 0-127");
  OUTLET_DOC(3, "program",
             "Program change, 1-128 — the wire's 0-127 plus one, which is the number a hardware "
             "front panel displays and the number '.midiformat' takes back.",
             "1-128");
  OUTLET_DOC(4, "aftertouch",
             "Channel aftertouch, 0-127: one value for the whole channel however many keys are "
             "held. Per-note pressure is the poly outlet.",
             "0-127");
  OUTLET_DOC(5, "bend",
             "Pitch bend, 0-127 with 64 at rest — the coarse byte of a 14-bit message, which is "
             "Max's reading and '.bendin''s. The fine byte is discarded here; '.xbendin' is the "
             "object that keeps it.",
             "0-127");
  OUTLET_DOC(6, "channel",
             "The channel the message arrived on, 1-16 rather than the 0-15 nibble on the wire. "
             "Sent before the typed outlet, so anything the value triggers downstream already "
             "knows which channel it is looking at.",
             "1-16");
  OUTLET_DOC(7, "raw",
             "Everything the decoding does not cover, as a list of byte values: system exclusive, "
             "song position, song select, MIDI time code, tune request, and the one-byte real-time "
             "messages (248 clock, 250 start, 251 continue, 252 stop, 255 reset). A real-time byte "
             "may arrive in the middle of another message and leaves here on its own without "
             "disturbing it. A dump longer than 128 bytes leaves in consecutive chunks, the first "
             "beginning with 240 and the last ending with 247 — the treatment 'MIDI::inHub' "
             "already gives a message too long for one transport event.",
             "0-255");
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

void mMidiParse::Byte(unsigned char value, YSE::THREAD thread) {
  if (value >= kFirstRealTime) {
    // A real-time message is one byte and may appear anywhere at all, including
    // between the bytes of another message. Reported at once, on its own, and
    // disturbing neither running status nor a dump in progress — which is why
    // it is built in `scratch` rather than in the raw buffer.
    scratch.clear();
    AppendNumber(scratch, (int)value);
    outputs[7].SendList(scratch, thread);
    return;
  }

  if (value >= kFirstStatus) {
    // Any status byte ends a system-exclusive dump, whether or not the device
    // bothered to send an EOX first. Hardware does exactly this when it is
    // interrupted, and a parser that waited for the EOX would swallow every
    // message after it.
    if (inSysEx) {
      if (value == kSysExEnd) RawByte(value, thread);
      RawFlush(thread);
      inSysEx = false;
      if (value == kSysExEnd) return;
    } else if (value == kSysExEnd) {
      // An EOX with no dump in front of it. Reported rather than swallowed: the
      // raw outlet is where the protocol goes, including the parts of it that
      // arrive out of order.
      RawBegin();
      RawByte(value, thread);
      RawFlush(thread);
      return;
    }

    systemStatus = 0;
    systemNeeded = 0;
    dataCount = 0;

    if (value >= kFirstSystem) {
      // Every system message clears running status. Real time does not, which
      // is handled above — that distinction is the whole reason a clock can
      // pass through a chord without breaking it.
      runningStatus = 0;
      RawBegin();
      RawByte(value, thread);
      if (value == kSysExStart) {
        inSysEx = true;
        return;
      }
      const int needed = SystemDataBytes(value);
      if (needed == 0) {
        RawFlush(thread);
        return;
      }
      systemStatus = value;
      systemNeeded = needed;
      return;
    }

    runningStatus = value;
    return;
  }

  // A data byte.
  if (inSysEx) {
    RawByte(value, thread);
    return;
  }

  if (systemStatus != 0) {
    RawByte(value, thread);
    systemNeeded--;
    if (systemNeeded <= 0) {
      RawFlush(thread);
      systemStatus = 0;
    }
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

void mMidiParse::Voice(unsigned char status, YSE::THREAD thread) {
  // Right to left, as the input family sends: the channel is out before
  // anything the typed outlet triggers downstream can ask for it.
  outputs[6].SendInt((int)(status & kChannelMask) + 1, thread);

  const int first = (int)(data[0] & kDataMask);
  const int second = (int)(data[1] & kDataMask);

  switch (status & kStatusMask) {
  case kNoteOn:
    Pair(0, first, second, thread);
    break;
  // Max's rule, shared with `.notein`: a note-off reports velocity 0 rather
  // than the release velocity it carries, so the two spellings of a release
  // are one shape downstream.
  case kNoteOff:
    Pair(0, first, 0, thread);
    break;
  case kPolyPressure:
    Pair(1, first, second, thread);
    break;
  case kControlChange:
    Pair(2, first, second, thread);
    break;
  case kProgramChange:
    outputs[3].SendInt(first + 1, thread);
    break;
  case kChannelPressure:
    outputs[4].SendInt(first, thread);
    break;
  // The wire sends the fine byte first; reporting the coarse one alone is
  // what makes this the 7-bit reading `.bendin` also gives.
  case kPitchBend:
    outputs[5].SendInt(second, thread);
    break;
  default:
    break;
  }
}

void mMidiParse::Pair(int outlet, int first, int second, YSE::THREAD thread) {
  scratch.clear();
  AppendNumber(scratch, first);
  AppendNumber(scratch, second);
  outputs[(std::size_t)outlet].SendList(scratch, thread);
}

void mMidiParse::RawBegin() {
  raw.clear();
  rawCount = 0;
}

void mMidiParse::RawByte(unsigned char value, YSE::THREAD thread) {
  if (rawCount >= RAW_CHUNK_BYTES) {
    // A dump longer than the buffer leaves in consecutive chunks rather than
    // growing the buffer (which would allocate on the audio thread) or losing
    // its tail (which would corrupt the dump silently).
    outputs[7].SendList(raw, thread);
    raw.clear();
    rawCount = 0;
  }
  AppendNumber(raw, (int)value);
  rawCount++;
}

void mMidiParse::RawFlush(YSE::THREAD thread) {
  if (rawCount == 0) return;
  outputs[7].SendList(raw, thread);
  raw.clear();
  rawCount = 0;
}

bool mMidiParse::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired this object's outlet
    // back into its inlet. Counted rather than spun on: this is a path the
    // audio callback takes, and the state machine is not re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mMidiParse::Leave() {
  busy.store(false, std::memory_order_release);
}

#undef className

// ─── .midiformat ────────────────────────────────────────────────────────────

#define className mMidiFormat

CONSTRUCT() {
  // Max's seven inlets in Max's order, so a patch brought across wires the
  // same, plus the raw passthrough after them — see the header for why it is
  // last rather than beside the other message inlets.
  ADD_IN_0;
  REG_INT_IN(FormatInt);
  REG_FLOAT_IN(FormatFloat);
  REG_LIST_IN(FormatList);

  for (int i = 1; i < 8; i++) {
    inputs.emplace_back(this, false, i);
    REG_INT_IN(FormatInt);
    REG_FLOAT_IN(FormatFloat);
    // A list only means something on the inlets that take two numbers and on
    // the raw passthrough. Registering it nowhere else keeps
    // GetAcceptedTypes() reporting the real contract — the `.zl` / `.pack`
    // discipline.
    if (i == 1 || i == 2 || i == 7) REG_LIST_IN(FormatList);
  }

  ADD_OUT_LIST;

  scratch.reserve(static_cast<std::size_t>(RAW_MAX_BYTES) * (FORMAT_INT_WIDTH + 1));

  ADD_DESCRIPTION(
      "Assembles structured data into raw MIDI messages — Max's 'midiformat' (issue #530), and the "
      "exact inverse of '.midiparse', inlet for outlet, so the two wire straight across and a "
      "patch can take a stream apart, edit the part it cares about and put it back together "
      "without ever spelling a status byte itself. It is also the combined form of the six "
      "single-purpose formatters ('.noteon', '.noteoff', '.controlchange', '.polypressure', "
      "'.channelpressure', '.programchange'): one box with one channel setting instead of six with "
      "six. Every inlet but the channel is hot — a message arriving at it produces a MIDI message "
      "at once, on whatever channel the channel inlet is holding, which is Max's rule and the "
      "reason the channel is the rightmost of Max's inlets. One complete message leaves the outlet "
      "as a list of byte values in decimal, '144 60 100' for a note-on: Max sends its bytes one at "
      "a time because Max's midiout reassembles them, while this patcher's transport is list text "
      "and '.seq' already documents a numeric byte list as the shape this object's output has, so "
      "a whole message per list is both the local convention and the safer one — three bytes that "
      "belong together cannot be split across two blocks by anything downstream. Program change "
      "takes 1-128 and pitch bend 0-127, the readings '.midiparse', '.pgmin' and '.bendin' report, "
      "so a round trip gives back the numbers it was given. Values are clamped into the ranges "
      "MIDI has rather than refused, so a patch that scales a controller into 0-140 produces "
      "sensible notes at the top of its range instead of silence. The one exception is the raw "
      "inlet, where a value outside 0-255 is not a byte at all and is dropped: clamping a status "
      "byte would change which message it is. The object opens no device and needs none, so it "
      "runs on every platform, and it allocates nothing on a message path — the outgoing text is "
      "built into a string reserved when the object is created.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "note",
            "The list 'pitch velocity', or a bare pitch with velocity 0. Velocity 0 is a release, "
            "which is how '.midiparse' reports one, so the pair round-trips; a real note-off "
            "message (status 128) is what '.noteoff' sends and is not produced here, exactly as in "
            "Max.",
            "0-127 0-127");
  INLET_DOC(1, "poly",
            "Polyphonic key pressure as the list 'pitch pressure' — pressure on one held key. A "
            "bare int is the pitch with pressure 0.",
            "0-127 0-127");
  INLET_DOC(2, "control",
            "Control change as the list 'controller value'. A bare int is the controller number "
            "with value 0.",
            "0-127 0-127");
  INLET_DOC(3, "program",
            "Program change, 1-128 — the number a hardware front panel displays, and the one "
            "'.midiparse' and '.pgmin' report. One is subtracted on the way to the wire.",
            "1-128");
  INLET_DOC(4, "aftertouch", "Channel aftertouch: one pressure value for the whole channel.",
            "0-127");
  INLET_DOC(5, "bend",
            "Pitch bend, 0-127 with 64 at rest — the coarse byte, which is Max's resolution and "
            "'.bendin''s. The fine byte of the message is sent as 0.",
            "0-127");
  INLET_DOC(6, "channel",
            "The channel every message is formatted on, 1-16. Cold: it stores and sends nothing, "
            "which is Max's rule for this inlet. Defaults to 1.",
            "1-16");
  INLET_DOC(7, "raw",
            "Byte values passed straight through to the outlet, as a list or one at a time. Not "
            "Max's inlet — it exists so the pair is a true inverse: '.midiparse''s rightmost "
            "outlet carries the system exclusive, song position and real-time traffic it could not "
            "decode, and without somewhere for that to go back in a patch could take a stream "
            "apart but not put the whole of it back together. A value outside 0-255 is not a byte "
            "and is dropped rather than clamped, and a list longer than 128 bytes is cut there.",
            "0-255");
  OUTLET_DOC(0, "midi",
             "One complete MIDI message per hot inlet message, as a list of byte values in "
             "decimal: '144 60 100' is a note-on, middle C, velocity 100 on channel 1. Feed it to "
             "'.seq' to record, or to '.midiparse' to read back what was built.",
             "0-255");
}

INT_IN(FormatInt) {
  if (!Enter()) return;
  switch (inlet) {
  case 0:
  case 1:
  case 2:
    // Max: a bare number on a two-number inlet is the first of the pair, the
    // second being 0 — a release, a released key and a controller at zero.
    Two(inlet, value, 0, thread);
    break;
  case 3:
    Emit(kProgramChange, Clamp(value, 1, 128) - 1, thread);
    break;
  case 4:
    Emit(kChannelPressure, Clamp7(value), thread);
    break;
  // The wire sends the fine byte first. This object is the 7-bit half of the
  // pair, so it sends 0 for it.
  case 5:
    Emit(kPitchBend, 0, Clamp7(value), thread);
    break;
  case 6:
    channel = Clamp(value, 1, 16);
    break;
  case 7:
    if (value < 0 || value > kByteMax) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      break;
    }
    scratch.clear();
    AppendNumber(scratch, value);
    outputs[0].SendList(scratch, thread);
    break;
  default:
    break;
  }
  Leave();
}

FLOAT_IN(FormatFloat) {
  FormatInt((int)value, inlet, thread);
}

LIST_IN(FormatList) {
  if (!Enter()) return;
  std::size_t cursor = 0;

  if (inlet == 7) {
    scratch.clear();
    int count = 0;
    int number = 0;
    bool refused = false;
    while (ReadIntArgAt(value, cursor, number)) {
      if (count >= RAW_MAX_BYTES) {
        refused = true;
        break;
      }
      if (number < 0 || number > kByteMax) {
        refused = true;
        continue;
      }
      AppendNumber(scratch, number);
      count++;
    }
    if (refused) dropped.fetch_add(1, std::memory_order_relaxed);
    if (count > 0) outputs[0].SendList(scratch, thread);
    Leave();
    return;
  }

  int first = 0;
  int second = 0;
  if (ReadIntArgAt(value, cursor, first)) {
    if (!ReadIntArgAt(value, cursor, second)) second = 0;
    Two(inlet, first, second, thread);
  }
  Leave();
}

void mMidiFormat::Two(int inlet, int first, int second, YSE::THREAD thread) {
  switch (inlet) {
  case 0:
    Emit(kNoteOn, Clamp7(first), Clamp7(second), thread);
    break;
  case 1:
    Emit(kPolyPressure, Clamp7(first), Clamp7(second), thread);
    break;
  case 2:
    Emit(kControlChange, Clamp7(first), Clamp7(second), thread);
    break;
  default:
    break;
  }
}

void mMidiFormat::Emit(unsigned char status, int first, int second, YSE::THREAD thread) {
  scratch.clear();
  AppendNumber(scratch, (int)(status | (unsigned char)(channel - 1)));
  AppendNumber(scratch, first);
  AppendNumber(scratch, second);
  outputs[0].SendList(scratch, thread);
}

void mMidiFormat::Emit(unsigned char status, int first, YSE::THREAD thread) {
  scratch.clear();
  AppendNumber(scratch, (int)(status | (unsigned char)(channel - 1)));
  AppendNumber(scratch, first);
  outputs[0].SendList(scratch, thread);
}

bool mMidiFormat::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mMidiFormat::Leave() {
  busy.store(false, std::memory_order_release);
}

#undef className
