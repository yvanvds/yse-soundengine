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
      "The MIDI channel the message arrived on, 1-16 — the numbering Max uses, the numbering the "
      "sending objects use, and the numbering printed on the hardware, rather than the 0-15 nibble "
      "on the wire. Sent first, before the outlets to its left, so anything the leftmost outlet "
      "triggers downstream already knows which channel it is looking at.";

} // namespace

// ─── .xbendin — fourteen bits as one number ─────────────────────────────────

mXBendIn::mXBendIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // bend
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION(
      "Extra-precision MIDI pitch-bend input — Max's 'xbendin' (issue #533). Reports the pitch "
      "wheel's position at the full resolution the message carries: 0-16383 with 8192 at rest, "
      "against '.bendin''s 0-127 with 64 at rest. The wire has always sent fourteen bits as a fine "
      "byte then a coarse one; '.bendin' reports the coarse byte alone because that is Max's "
      "reading and what '.midiparse' shares, and this object reports both combined. The difference "
      "is audible rather than academic — over the usual two-semitone bend range seven bits is a "
      "step of about three cents, which is a staircase on any bend slow enough to hear. The two "
      "are separate objects rather than one with a mode because the number means something "
      "different in each: a patch scaling 0-127 and a patch scaling 0-16383 are not the same "
      "patch. Both outlets fire right to left, channel first. A 'channel' argument filters and, "
      "unlike Max, leaves the channel outlet in place. Events cross from the device backend's "
      "thread on a bounded lock-free queue and are drained once per audio block, so nothing "
      "allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "bend",
             "The wheel position at full resolution, 0-16383, centred at 8192: 0 is fully down, "
             "16383 fully up. Subtract 8192 before scaling, as a patch does with '.bendin''s 64.",
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

  ADD_DESCRIPTION(
      "Extra-precision MIDI pitch-bend input reported as separate bytes — Max's 'xbendin2' (issue "
      "#533). The same message '.xbendin' decodes, given as its most and least significant bytes "
      "rather than combined into one 0-16383 number: the MSB is '.bendin''s 0-127 reading with 64 "
      "at rest, and the LSB is the fine byte underneath it. Use '.xbendin' when the patch wants a "
      "number to scale; use this one when the two halves are wanted apart — a coarse bend with a "
      "separate fine refinement, or a byte-for-byte round trip through '.xbendout2', which takes "
      "exactly this shape back in without ever combining and re-splitting the value. The three "
      "outlets fire right to left, so whatever the MSB triggers downstream already sees the LSB "
      "and channel that came with it. A 'channel' argument filters and, unlike Max, leaves the "
      "channel outlet in place. Events cross from the device backend's thread on a bounded "
      "lock-free queue and are drained once per audio block, so nothing allocates, locks or blocks "
      "on either side.");
  OUTLET_DOC(0, "msb",
             "The coarse byte, 0-127 with 64 at rest — the same number '.bendin' reports. Sent "
             "last of the three.",
             "0-127");
  OUTLET_DOC(1, "lsb",
             "The fine byte, 0-127, which is the 128 steps between one MSB value and the next. "
             "Sent as it arrives on the wire, where it precedes the coarse byte.",
             "0-127");
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

  ADD_DESCRIPTION(
      "MIDI note input with release velocity — Max's 'xnotein' (issue #533). Everything '.notein' "
      "reports, plus the one number it throws away: a note-off message carries a velocity byte of "
      "its own, saying how fast the key came back up, and '.notein' discards it in order to report "
      "every release as velocity 0. That number is the only expressive gesture in MIDI that "
      "happens after a note is already over, and it is what shapes a release on any voice whose "
      "release is not fixed — which YSE's ADSR-based voices are. A note-on reports its velocity "
      "with release velocity 0; a note-off (status 0x80) reports velocity 0 and its own velocity "
      "byte as the release, so '.notein''s test for a release — zero on the velocity outlet — "
      "still reads the same here; a note-on with velocity 0, the other spelling hardware uses for "
      "a release, reports 0 for both, since no release velocity was sent and inventing one would "
      "be worse than reporting none. The four outlets fire right to left, so whatever the pitch "
      "triggers downstream already sees the velocities and channel that belong to it. A 'channel' "
      "argument filters and, unlike Max, leaves the channel outlet in place. Events cross from the "
      "device backend's thread on a bounded lock-free queue and are drained once per audio block, "
      "so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "pitch",
             "The note number, 0-127, sent last of the four so the velocities and channel that "
             "belong to it are already out when it triggers anything. Middle C is 60.",
             "0-127");
  OUTLET_DOC(1, "velocity",
             "How hard the key was struck, 0-127, and 0 for a release of either spelling — the "
             "same reading '.notein' gives, so a patch tests for a release the same way here.",
             "0-127");
  OUTLET_DOC(2, "release",
             "How fast the key came back up, 0-127, on a note-off that carries one; 0 on a "
             "note-on and on a release spelled as a note-on with velocity 0, neither of which "
             "sends a release velocity at all.",
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

  ADD_DESCRIPTION(
      "Extra-precision MIDI control-change input — Max's 'xctlin' (issue #533). MIDI pairs "
      "controller n (0-31) with controller n+32 as the coarse and fine halves of one fourteen-bit "
      "value; a '.ctlin' sees two unrelated controllers moving and this object sees one, reporting "
      "0-16383 on the coarse half's number. 128 steps is audibly steppy on a filter sweep — the "
      "zipper noise a patch hears on any slow move — and this pairing is what a control surface "
      "with real resolution actually sends. A value is emitted on *both* halves, not only on the "
      "fine one: the MSB emits at once with the fine byte read as 0, which is the MIDI "
      "specification's own recommended practice, and the LSB that follows emits again with the "
      "refined value. Waiting for the pair would leave the object silent forever on the many "
      "devices that send only the coarse half, which is a far worse failure than one coarse value "
      "followed immediately by its refinement. The stored coarse byte is kept per channel as well "
      "as per controller, so two keyboards on different channels cannot cross-contaminate each "
      "other. Controllers 64-127 have no fine half and are not reported here at all — those are "
      "'.ctlin''s, and giving a sustain pedal a fabricated fourteen-bit value would be a lie. The "
      "three outlets fire right to left. A 'channel' argument filters by channel and a second "
      "argument by coarse controller number; unlike Max, neither removes an outlet. Events cross "
      "from the device backend's thread on a bounded lock-free queue and are drained once per "
      "audio block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "value",
             "The controller's new value at full resolution, 0-16383, sent last of the three so "
             "the controller number and channel that identify it are already out when it triggers "
             "anything. A coarse-only device covers the range in steps of 128.",
             "0-16383");
  OUTLET_DOC(1, "controller",
             "Which controller moved, 0-31 — always the coarse half's number, whichever half of "
             "the pair carried the message. Constant when the object was given a controller "
             "argument, which is why the outlet is kept in that case.",
             "0-31");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
  PARAM_DOC("controller", "-1",
            "Coarse controller number to accept, 0-31, or -1 for every paired controller — the "
            "default. Not 0 for 'any': controller 0 is Bank Select, a controller a patch may "
            "legitimately want on its own. Give the coarse number even though the fine half "
            "arrives as that number plus 32; the pair is one controller and it is named by its "
            "coarse half. Anything outside 0-31 is read as -1, so a stray argument means 'every "
            "controller' rather than silencing the object outright.",
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

  ADD_DESCRIPTION(
      "Raw MIDI input framed into whole messages — Max's 'xmidiin' (issue #533). '.midiin' hands a "
      "patch one int per byte with nothing interpreted, which leaves the patch holding a stream "
      "with no boundaries in it: where one message ends and the next begins is something it has to "
      "work out, and a system-exclusive dump longer than the transport's event size arrives as "
      "consecutive chunks with nothing marking them as one message. This object does that framing. "
      "Every complete message leaves the outlet as one list of byte values in decimal — '144 60 "
      "100' for a note-on — which is '.midiformat''s output shape and '.midiparse''s, '.seq''s and "
      "'.midiout''s input shape, so it wires straight across. A dump split across four transport "
      "events arrives as one list, which is the 'messages split across packets' the object is "
      "named for. Unlike Max, which spells this outlet a byte at a time like 'midiin''s, the list "
      "is per message: bytes handed out one at a time carry no boundary no matter who reassembled "
      "them, so the framing would be invisible. Running status is honoured and the omitted status "
      "byte put back in front of each message; a real-time byte (248-255) is emitted at once on "
      "its own and disturbs nothing, since it may legally appear between any two bytes of any "
      "other message; a new status byte abandons an unfinished message rather than waiting for "
      "data that is not coming. A message longer than 256 bytes leaves in consecutive lists of "
      "that size, which is what keeps the object's memory fixed — nothing is dropped. Events cross "
      "from the device backend's thread on a bounded lock-free queue and are drained once per "
      "audio block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "midi",
             "One complete MIDI message per list, as byte values in decimal: status byte first, "
             "then its data bytes. Real-time messages arrive as one-byte lists. A system-exclusive "
             "dump arrives whole however many transport packets carried it, or in consecutive "
             "256-byte lists when it is longer than that.",
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
