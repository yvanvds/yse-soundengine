#include "mMidiRpnOut.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

namespace {

  int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
  }

  // A channel outside 0-15 would carry into the status nibble and make the
  // packet a different message altogether, so it is clamped rather than added
  // in raw. `.bendout`'s rule (#532), applied here for the same reason.
  int SafeChannel(int channel) {
    return Clamp(channel, 0, 15);
  }

  // The selecting controller pairs. Registered parameter numbers are selected
  // with 101/100 and non-registered ones with 99/98; both are written with the
  // Data Entry pair, which is what makes the two objects one body.
  constexpr int kRpnSelectMsb = 101;
  constexpr int kRpnSelectLsb = 100;
  constexpr int kNrpnSelectMsb = 99;
  constexpr int kNrpnSelectLsb = 98;

  constexpr char kChannelDoc[] = "MIDI channel, counted from 0: 0 is channel 1.";

  constexpr char kValueInletDoc[] =
      "The value to write; sends all four messages. A device that reads only the coarse byte "
      "sees the value in steps of 128.";

  constexpr char kOutletDoc[] =
      "Four control-change messages, for .midiout: the parameter number's coarse and fine bytes, "
      "then the value's coarse byte on controller 6 and fine byte on controller 38.";

} // namespace

// ─── the shared body ────────────────────────────────────────────────────────

#define className mRpnOutBase

mRpnOutBase::mRpnOutBase(int selectMsbController, int selectLsbController)
  : pObject(false), selectMsb(selectMsbController), selectLsb(selectLsbController) {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_IN_1;
  REG_INT_IN(SetIntParameter);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = 0;
  cvalue = 0;
  parameter = 0;

  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "value", kValueInletDoc, "0-16383");
  OUTLET_DOC(0, "midi", kOutletDoc, "");
  PARAM_DOC("channel", "0", kChannelDoc, "0-15");
}

INT_IN(SetIntValue) {
  cvalue = Clamp((int)value, 0, MAX_14BIT);
}

INT_IN(SetIntParameter) {
  parameter = Clamp((int)value, 0, MAX_14BIT);
}

CALC() {
  const char status = (char)(0xB0 + SafeChannel(channel));

  // One three-character string, rewritten and sent four times. Short-string
  // storage, so this allocates nothing.
  std::string message = "000";
  message[0] = status;

  // The selection first, and re-sent on every value: the receiving device has
  // exactly one selected parameter, and anything else on the cable can move it
  // between two values from this box.
  message[1] = (char)selectMsb;
  message[2] = (char)((parameter >> 7) & 0x7F);
  outputs[0].SendList(message, thread);

  message[1] = (char)selectLsb;
  message[2] = (char)(parameter & 0x7F);
  outputs[0].SendList(message, thread);

  // Then the value, coarse byte first — the order the specification requires,
  // and the order that lets a device which ignores the fine byte still land on
  // the right coarse value.
  message[1] = (char)DATA_ENTRY_MSB;
  message[2] = (char)((cvalue >> 7) & 0x7F);
  outputs[0].SendList(message, thread);

  message[1] = (char)DATA_ENTRY_LSB;
  message[2] = (char)(cvalue & 0x7F);
  outputs[0].SendList(message, thread);
}

#undef className

// ─── .rpnout ────────────────────────────────────────────────────────────────

mRpnOut::mRpnOut() : mRpnOutBase(kRpnSelectMsb, kRpnSelectLsb) {
  ADD_DESCRIPTION("Writes a 14-bit value to a registered parameter (RPN), such as pitch-bend "
                  "range, as four MIDI control-change messages for .midiout.");
  INLET_DOC(1, "parameter",
            "Registered parameter number, stored for the next value: 0 pitch-bend range, 1 fine "
            "tuning, 2 coarse tuning, 3 tuning program, 4 tuning bank, 16383 null.",
            "0-16383");
}

// ─── .nrpnout ───────────────────────────────────────────────────────────────

mNrpnOut::mNrpnOut() : mRpnOutBase(kNrpnSelectMsb, kNrpnSelectLsb) {
  ADD_DESCRIPTION("Writes a 14-bit value to a non-registered parameter (NRPN), a device's own "
                  "parameter, as four MIDI control-change messages for .midiout.");
  INLET_DOC(1, "parameter",
            "Non-registered parameter number, stored for the next value. What each number means is "
            "in the receiving device's documentation.",
            "0-16383");
}
