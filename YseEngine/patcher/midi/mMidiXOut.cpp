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

  ADD_DESCRIPTION("Builds a 14-bit MIDI pitch-bend message for .midiout: 0-16383 with 8192 at "
                  "rest.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "bend", "Pitch bend, 8192 at rest; sends the message.", "0-16383");
  OUTLET_DOC(0, "midi", "The pitch-bend message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
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

  ADD_DESCRIPTION("Builds a 14-bit MIDI pitch-bend message for .midiout from its coarse and fine "
                  "bytes, the form .xbendin2 reports.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "msb", "Coarse byte, 64 at rest; sends the message.", "0-127");
  INLET_DOC(1, "lsb", "Fine byte, stored for the next coarse byte.", "0-127");
  OUTLET_DOC(0, "midi", "The pitch-bend message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
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

  ADD_DESCRIPTION("Sends a 14-bit controller value as two MIDI control-change messages for "
                  ".midiout: the coarse half on controller 0-31, then the fine half on that "
                  "number plus 32.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "value", "Controller value; sends both messages.", "0-16383");
  INLET_DOC(1, "controller",
            "Coarse controller number, stored for the next value. The fine half goes to this "
            "number plus 32.",
            "0-31");
  OUTLET_DOC(0, "midi", "Two control-change messages, coarse then fine, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
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

  ADD_DESCRIPTION("Builds a MIDI note message with release velocity for .midiout. A velocity above "
                  "0 sends a note-on; 0 sends a note-off carrying the release velocity.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "pitch", "Note number; sends the message.", "0-127");
  INLET_DOC(1, "velocity",
            "Note velocity, stored for the next pitch. 0 makes the message a note-off.", "0-127");
  INLET_DOC(2, "release",
            "Release velocity, stored for the next pitch and used only when the velocity is 0. "
            "Starts at 64.",
            "0-127");
  OUTLET_DOC(0, "midi", "The note-on or note-off message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
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
