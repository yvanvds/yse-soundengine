#include "mMidiXOut.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

namespace {

  int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
  }

  // A channel outside 0-15 would carry into the status nibble and make the
  // packet a different message altogether — a bend arriving as a note-off is
  // worse than a bend on the wrong channel — so it is clamped rather than
  // added in raw. This is `.bendout`'s rule (#532), applied to all four of
  // these for the same reason.
  int SafeChannel(int channel) {
    return Clamp(channel, 0, 15);
  }

} // namespace

// ─── .xbendout — fourteen bits from one number ──────────────────────────────

#define className mXBendOut

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = 0;
  // 8192 is the pitch wheel at rest at this resolution, which is the honest
  // resting value for the field even though the inlet overwrites it before
  // every send.
  cvalue = 8192;

  ADD_DESCRIPTION(
      "Extra-precision MIDI Pitch Bend message generator — Max's 'xbendout' (issue #533). A value "
      "on the inlet stores it and emits a three-byte packet on the channel given as the creation "
      "argument, using all fourteen bits the message carries: 0-16383 with 8192 at rest, against "
      "'.bendout''s 0-127 with 64 at rest. The wire order is the fine byte then the coarse one, "
      "and this object fills both, where '.bendout' sends the value as the coarse byte and 0 as "
      "the fine one. The difference is a step of about a fortieth of a cent instead of three "
      "cents over the usual two-semitone range, which is what stops a slow bend sounding like a "
      "staircase. Out-of-range values are clamped rather than refused, as they are on the rest of "
      "the family, so a patch scaling a controller into a wider range bends to the extremes "
      "instead of falling silent. The output is a raw byte list for '.midiout'.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "bend",
            "Pitch bend position at full resolution, 8192 at rest (also fires the "
            "output).",
            "0-16383");
  OUTLET_DOC(0, "midi", "Encoded MIDI Pitch Bend message (status, fine byte, coarse byte).", "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntValue) {
  cvalue = Clamp((int)value, 0, 16383);
}

CALC() {
  // Three chars: short-string storage, so nothing is allocated here.
  std::string message = "000";
  message[0] = (char)(0xE0 + SafeChannel(channel));
  // Fine byte first — the wire's order, and the half `.bendout` leaves at 0.
  message[1] = (char)(cvalue & 0x7F);
  message[2] = (char)((cvalue >> 7) & 0x7F);
  outputs[0].SendList(message, thread);
}

#undef className

// ─── .xbendout2 — fourteen bits from its two bytes ──────────────────────────

#define className mXBendOut2

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntMsb);

  ADD_IN_1;
  REG_INT_IN(SetIntLsb);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = 0;
  msb = 64; // the wheel at rest, coarse half
  lsb = 0;

  ADD_DESCRIPTION(
      "Extra-precision MIDI Pitch Bend message generator taking separate bytes — Max's 'xbendout2' "
      "(issue #533). The same message '.xbendout' sends, given as its coarse and fine bytes rather "
      "than as one 0-16383 number: inlet 0 is the MSB, which is '.bendout''s 0-127 with 64 at "
      "rest, and inlet 1 is the fine byte underneath it. This is '.xbendin2''s partner — the two "
      "halves come out of that object and go into this one unchanged, so a wheel position "
      "round-trips byte for byte without ever being combined and re-split. Inlet 0 is hot and "
      "inlet 1 stores until the next MSB, which is the arrangement every sender in this family "
      "has. Both bytes are clamped to 0-127: a byte with its top bit set would be read as a status "
      "byte by the receiving device and would end the message early. The output is a raw byte list "
      "for '.midiout'.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "msb", "Coarse bend byte, 64 at rest (also fires the output).", "0-127");
  INLET_DOC(1, "lsb", "Fine bend byte, stored until the next MSB.", "0-127");
  OUTLET_DOC(0, "midi", "Encoded MIDI Pitch Bend message (status, fine byte, coarse byte).", "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntMsb) {
  msb = Clamp((int)value, 0, 127);
}

INT_IN(SetIntLsb) {
  lsb = Clamp((int)value, 0, 127);
}

CALC() {
  std::string message = "000";
  message[0] = (char)(0xE0 + SafeChannel(channel));
  message[1] = (char)lsb;
  message[2] = (char)msb;
  outputs[0].SendList(message, thread);
}

#undef className

// ─── .xctlout — the coarse/fine controller pair ─────────────────────────────

#define className mXCtlOut

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_IN_1;
  REG_INT_IN(SetIntController);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = 0;
  cvalue = 0;
  controller = 0;

  ADD_DESCRIPTION(
      "14-bit MIDI Control Change message generator — Max's 'xctlout' (issue #533). MIDI pairs "
      "controller n (0-31) with controller n+32 as the coarse and fine halves of one fourteen-bit "
      "value, and this object sends both: a value on the inlet emits two complete messages, the "
      "coarse one first and the fine one immediately after, which is the order the specification "
      "requires and the order every receiver expects. '.controlchange' sends one seven-bit message "
      "instead, and 128 steps is audibly steppy on a filter sweep — the zipper noise a patch hears "
      "on any slow move. The two messages leave as two separate lists rather than one six-byte "
      "list because '.midiout' hands each list it receives to the device as a single MIDI message, "
      "so six bytes together would arrive as one malformed message rather than two good ones. Only "
      "the 32 paired controllers can be addressed: a number above 31 has no fine half, and sending "
      "its 'LSB' would be writing to an unrelated controller 32 higher up, so the number is "
      "clamped into 0-31. The value is clamped into 0-16383 rather than refused, as everywhere in "
      "this family. The output is a raw byte list for '.midiout'.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "value", "Controller value at full resolution (also fires both messages).",
            "0-16383");
  INLET_DOC(1, "controller",
            "Coarse controller number, stored until the next value. The fine half is sent on this "
            "number plus 32.",
            "0-31");
  OUTLET_DOC(0, "midi",
             "Encoded MIDI Control Change messages: the coarse one on the controller number, then "
             "the fine one on that number plus 32.",
             "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntValue) {
  cvalue = Clamp((int)value, 0, 16383);
}

INT_IN(SetIntController) {
  controller = Clamp((int)value, 0, kXPairedControllers - 1);
}

CALC() {
  const char status = (char)(0xB0 + SafeChannel(channel));

  std::string message = "000";
  message[0] = status;
  message[1] = (char)controller;
  message[2] = (char)((cvalue >> 7) & 0x7F);
  outputs[0].SendList(message, thread);

  // The fine half, as its own message. Sent second, which is what lets a
  // receiver that only understands the coarse controller behave sensibly.
  message[0] = status;
  message[1] = (char)(controller + kXPairedControllers);
  message[2] = (char)(cvalue & 0x7F);
  outputs[0].SendList(message, thread);
}

#undef className

// ─── .xnoteout — notes with release velocity ────────────────────────────────

#define className mXNoteOut

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntPitch);

  ADD_IN_1;
  REG_INT_IN(SetIntVelocity);

  ADD_IN_2;
  REG_INT_IN(SetIntRelease);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = pitch = velocity = 0;
  // 64 is the conventional "no particular release" value, and the one a device
  // that does not measure release velocity sends.
  release = 64;

  ADD_DESCRIPTION(
      "MIDI note message generator with release velocity — Max's 'xnoteout' (issue #533). "
      "'.noteon' and '.noteoff' are two boxes and neither can send a release velocity: '.noteoff' "
      "sends a note-off with velocity 0. This object is both of them plus that number. A velocity "
      "of 0 on the middle inlet sends a real note-off (status 0x80) carrying the release velocity "
      "from the third inlet; any other velocity sends a note-on. Release velocity says how fast "
      "the key came back up — the only expressive gesture in MIDI that happens after a note is "
      "already over, and what shapes the release of any voice whose release is not fixed, which "
      "YSE's ADSR-based voices are. It is the sending half of what '.xnotein' reports. Inlet 0 is "
      "hot and the other two store until the next pitch, which is the shape '.noteon' already has. "
      "All three values are clamped into 0-127 rather than refused, as everywhere in this family. "
      "The output is a raw byte list for '.midiout'.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "pitch", "MIDI note number (also fires the output).", "0-127");
  INLET_DOC(1, "velocity",
            "Note velocity; 0 makes the message a note-off carrying the release "
            "velocity. Stored until the next pitch.",
            "0-127");
  INLET_DOC(2, "release",
            "Release velocity, used only when the velocity is 0. Stored until the next pitch.",
            "0-127");
  OUTLET_DOC(0, "midi",
             "Encoded MIDI Note-On (status, pitch, velocity) or, when the velocity is 0, Note-Off "
             "(status, pitch, release velocity).",
             "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntPitch) {
  pitch = Clamp((int)value, 0, 127);
}

INT_IN(SetIntVelocity) {
  velocity = Clamp((int)value, 0, 127);
}

INT_IN(SetIntRelease) {
  release = Clamp((int)value, 0, 127);
}

CALC() {
  const int ch = SafeChannel(channel);
  const bool off = velocity == 0;

  std::string message = "000";
  message[0] = (char)((off ? 0x80 : 0x90) + ch);
  message[1] = (char)pitch;
  // The whole point of the object: a note-off carries the release velocity
  // where a note-on carries the striking one.
  message[2] = (char)(off ? release : velocity);
  outputs[0].SendList(message, thread);
}

#undef className
