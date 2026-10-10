#include "headers/defines.hpp"
// See the matching guard in mMidiXIn.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiXIn.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format again, spelled locally as mMidiIn.cpp and mMidiCodec.cpp
  // both do. Sharing seven constants that have not moved since 1983 across
  // three files with different platform guards would cost more than it saves.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kNoteOff = 0x80;
  constexpr unsigned char kNoteOn = 0x90;
  constexpr unsigned char kControlChange = 0xB0;
  constexpr unsigned char kProgramChange = 0xC0;
  constexpr unsigned char kChannelPressure = 0xD0;
  constexpr unsigned char kPitchBend = 0xE0;

  constexpr unsigned char kFirstSystem = 0xF0;
  constexpr unsigned char kSysExStart = 0xF0;
  constexpr unsigned char kSysExEnd = 0xF7;
  constexpr unsigned char kFirstRealTime = 0xF8;

  // Whether `event` is a channel-voice message of `status` with at least
  // `needed` bytes. A chunk of a longer message can land here, so the length
  // test is what keeps it from being read as a short message that happens to
  // begin with the right nibble.
  bool IsVoice(const YSE::MIDI::inEvent& event, unsigned char status, unsigned char needed) {
    if (event.len < needed) return false;
    return (event.bytes[0] & kStatusMask) == status;
  }

  int Data(const YSE::MIDI::inEvent& event, std::size_t index) {
    return static_cast<int>(event.bytes[index] & kDataMask);
  }

  unsigned char Nibble(const YSE::MIDI::inEvent& event) {
    return static_cast<unsigned char>(event.bytes[0] & kChannelMask);
  }

  // How many data bytes a channel-voice message of this status carries.
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

  // Appends one number to a list being built, with the separator a list needs
  // between its atoms and none in front of its first. Allocation-free as long
  // as the caller reserved the string; `WriteInt` is the patcher's own decimal
  // writer, which neither allocates nor reads locale state.
  void AppendNumber(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    if (!out.empty()) out.push_back(' ');
    out.append(digits, written);
  }

  constexpr char kChannelOutletDoc[] =
      "The channel the message arrived on, 1-16. Sent before the outlets to its left.";

} // namespace

// ─── .xbendin — fourteen bits as one number ─────────────────────────────────

mXBendIn::mXBendIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // bend
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI pitch-bend input at full 14-bit resolution: reports the pitch wheel as "
                  "0-16383 with 8192 at rest.");
  OUTLET_DOC(0, "bend", "The wheel position: 0 fully down, 8192 at rest, 16383 fully up.",
             "0-16383");
  OUTLET_DOC(1, "channel", kChannelOutletDoc, "1-16");
}

void mXBendIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kPitchBend, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  // Byte 1 is the LSB and byte 2 the MSB — the wire sends the fine byte
  // first. Combining them is the whole difference from '.bendin'.
  const int value = (Data(event, 2) << 7) | Data(event, 1);

  outputs[1].SendInt(ChannelNumber(nibble), thread);
  outputs[0].SendInt(value, thread);
}

// ─── .xbendin2 — fourteen bits as the two bytes that carry them ─────────────

mXBendIn2::mXBendIn2() : mMidiChannelInBase() {
  ADD_OUT_INT; // MSB
  ADD_OUT_INT; // LSB
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI pitch-bend input reported as its two bytes: the coarse byte, with 64 at "
                  "rest, and the fine byte below it. Pairs with .xbendout2.");
  OUTLET_DOC(0, "msb", "The coarse byte, 64 at rest: the value .bendin reports. Sent last.",
             "0-127");
  OUTLET_DOC(1, "lsb", "The fine byte: 128 steps between one coarse value and the next.", "0-127");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
}

void mXBendIn2::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kPitchBend, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt(Data(event, 1), thread); // LSB — first on the wire
  outputs[0].SendInt(Data(event, 2), thread); // MSB
}

// ─── .xnotein — notes with release velocity ─────────────────────────────────

mXNoteIn::mXNoteIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // pitch
  ADD_OUT_INT; // velocity
  ADD_OUT_INT; // release velocity
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI note input with release velocity. Reports pitch, velocity, release "
                  "velocity and channel; a note-off reports velocity 0 and how fast the key came "
                  "up.");
  OUTLET_DOC(0, "pitch", "The note number; middle C is 60. Sent last.", "0-127");
  OUTLET_DOC(1, "velocity", "How hard the key was struck, or 0 for a release, as .notein reports.",
             "0-127");
  OUTLET_DOC(2, "release",
             "How fast the key came up, from a note-off message. 0 on a note-on, including one "
             "with velocity 0.",
             "0-127");
  OUTLET_DOC(3, "channel", kChannelOutletDoc, "1-16");
}

void mXNoteIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  const bool on = IsVoice(event, kNoteOn, 3);
  const bool off = IsVoice(event, kNoteOff, 3);
  if (!on && !off) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  const int carried = Data(event, 2);
  const int velocity = on ? carried : 0;
  // Only a real note-off carries a release velocity. A note-on with velocity 0
  // is a release with no release velocity in it, and reporting anything but 0
  // for that would be a number the device never sent.
  const int release = off ? carried : 0;

  outputs[3].SendInt(ChannelNumber(nibble), thread);
  outputs[2].SendInt(release, thread);
  outputs[1].SendInt(velocity, thread);
  outputs[0].SendInt(Data(event, 1), thread);
}

// ─── .xctlin — controllers at fourteen bits ─────────────────────────────────

mXCtlIn::mXCtlIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // value
  ADD_OUT_INT; // controller number
  ADD_OUT_INT; // channel

  ADD_PARAM(controller);
  controller = -1;

  ADD_DESCRIPTION("14-bit MIDI control-change input. Combines controllers 0-31 with their fine "
                  "partners 32-63 and reports each move as one value of 0-16383.");
  OUTLET_DOC(0, "value",
             "The controller's value. Sent last. A device that sends only the coarse controller "
             "moves in steps of 128.",
             "0-16383");
  OUTLET_DOC(1, "controller", "The coarse controller number, whichever of the pair moved.", "0-31");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
  PARAM_DOC("controller", "-1",
            "Coarse controller number to listen to, 0-31, or -1 for all of them. Any other value "
            "means all of them.",
            "-1 to 31");
}

void mXCtlIn::ParseParams() {
  mMidiChannelInBase::ParseParams();
  const Int value = controller.load();
  if (value < 0 || value >= PAIRED_CONTROLLERS) controller = -1;
}

void mXCtlIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kControlChange, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  const int number = Data(event, 1);
  // Only the 64 controllers that form the 32 pairs mean anything here.
  if (number >= 2 * PAIRED_CONTROLLERS) return;

  const bool coarse = number < PAIRED_CONTROLLERS;
  const int pair = coarse ? number : number - PAIRED_CONTROLLERS;

  const Int filter = controller.load();
  if (filter >= 0 && filter != pair) return;

  const int byte = Data(event, 2);
  int value = 0;
  if (coarse) {
    // An MSB resets the fine byte, which is the specification's rule and what
    // makes a coarse-only device work at all.
    msb[nibble][pair] = static_cast<unsigned char>(byte);
    value = byte << 7;
  } else {
    value = (static_cast<int>(msb[nibble][pair]) << 7) | byte;
  }

  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt(pair, thread);
  outputs[0].SendInt(value, thread);
}

// ─── .xmidiin — the raw stream, framed into messages ────────────────────────

mXMidiIn::mXMidiIn() : mMidiInBase() {
  ADD_OUT_LIST;

  // The object's whole allocation, taken on the control thread before it is
  // wired: from here on a byte arriving on the audio thread only ever appends
  // into storage that already exists.
  scratch.reserve(static_cast<std::size_t>(MAX_BYTES) * (FORMAT_INT_WIDTH + 1));

  ADD_DESCRIPTION("Raw MIDI input framed into messages. Each complete message received on a MIDI "
                  "input port leaves as one list of byte values, such as 144 60 100.");
  OUTLET_DOC(0, "midi",
             "One message per list: the status byte, then its data bytes. A clock byte is a list "
             "of one; a system-exclusive dump longer than 256 bytes leaves in 256-byte lists.",
             "0-255 per byte");
}

void mXMidiIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  for (std::size_t i = 0; i < event.len; i++) {
    Byte(event.bytes[i], thread);
  }
}

void mXMidiIn::Flush(YSE::THREAD thread) {
  if (count == 0) return;
  scratch.clear();
  for (int i = 0; i < count; i++) {
    AppendNumber(scratch, static_cast<int>(message[i]));
  }
  count = 0;
  outputs[0].SendList(scratch, thread);
}

void mXMidiIn::Byte(unsigned char value, YSE::THREAD thread) {
  if (value >= kFirstRealTime) {
    // One byte, and it may appear anywhere at all — including between the
    // bytes of a message this object is part-way through collecting. Sent
    // straight out of `scratch` without touching the byte buffer or `count`,
    // so the interrupted message carries on where it left off.
    scratch.clear();
    AppendNumber(scratch, static_cast<int>(value));
    outputs[0].SendList(scratch, thread);
    return;
  }

  if (value >= 0x80) {
    if (value == kSysExEnd) {
      // The only status byte that *completes* rather than starts. Outside a
      // dump it belongs to nothing and is dropped.
      if (!inSysEx) return;
      message[count++] = value;
      inSysEx = false;
      Flush(thread);
      return;
    }

    // Any other status abandons whatever was in progress: hardware
    // interrupted mid-message starts sending something else rather than
    // finishing what it began.
    count = 0;
    inSysEx = false;

    if (value == kSysExStart) {
      // A dump clears running status (it is a system message) and has no
      // length to expect — it ends at its 0xF7 or at the buffer's edge.
      runningStatus = 0;
      inSysEx = true;
      message[count++] = value;
      return;
    }

    if (value >= kFirstSystem) {
      runningStatus = 0;
      expected = 1 + SystemDataBytes(value);
    } else {
      // Channel voice: the one kind running status applies to.
      runningStatus = value;
      expected = 1 + VoiceDataBytes(value);
    }
    message[count++] = value;
    if (count >= expected) Flush(thread);
    return;
  }

  // A data byte.
  if (inSysEx) {
    message[count++] = value;
    // A dump is arbitrarily long and this buffer is not. Emitting a full one
    // and carrying on keeps the memory fixed and loses nothing, which is
    // `.midiparse`'s treatment of the same case.
    if (count >= MAX_BYTES) Flush(thread);
    return;
  }

  if (count == 0) {
    // No message open. Running status is what a chord's second and later
    // notes arrive under; with none, this byte belongs to nothing.
    if (runningStatus == 0) return;
    expected = 1 + VoiceDataBytes(runningStatus);
    message[count++] = runningStatus;
  }

  message[count++] = value;
  if (count >= expected) Flush(thread);
}

#endif
