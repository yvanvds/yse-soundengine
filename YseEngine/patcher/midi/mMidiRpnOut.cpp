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

  constexpr char kChannelDoc[] = "MIDI channel offset (0-based).";

  constexpr char kValueInletDoc[] =
      "The value to write, at the full 14-bit resolution the Data Entry pair carries (also fires "
      "all four messages). A device that reads only the coarse half sees the value in steps of "
      "128, "
      "which is the same reading it would give a plain controller.";

  constexpr char kOutletDoc[] =
      "Four encoded MIDI Control Change messages, one list each and in order: the parameter "
      "number's coarse byte, its fine byte, the value's coarse byte on controller 6, and its fine "
      "byte on controller 38. Four lists rather than one, because '.midiout' sends each list it "
      "receives as a single MIDI message.";

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
  ADD_DESCRIPTION(
      "Registered parameter number sender — Max's 'rpnout' (issue #534). MIDI has 128 controller "
      "numbers and a synthesiser has far more than 128 parameters; registered and non-registered "
      "parameter numbers are the escape hatch, and this is the registered half. A value on the "
      "inlet emits four complete Control Change messages: controller 101 and controller 100 select "
      "the 14-bit parameter number given on the right inlet, then controllers 6 and 38 write the "
      "14-bit value. Registered numbers are the ones the MIDI specification itself assigns and "
      "mean "
      "the same thing on every device that implements them — 0 pitch-bend sensitivity, 1 fine "
      "tuning, 2 coarse tuning, 3 tuning program select, 4 tuning bank select — so a patch can "
      "widen a synth's bend range without consulting a manufacturer's chart. Manufacturer-specific "
      "parameters are '.nrpnout''s. The selection is re-sent with every value rather than only "
      "when "
      "the parameter number changes: the receiving device has one selected parameter, and anything "
      "else on the cable can move it between two values from this box, which is the failure this "
      "mechanism is notorious for. The four messages leave as four separate lists because "
      "'.midiout' sends each list it receives as a single MIDI message, so twelve bytes together "
      "would arrive as one malformed message rather than four good ones. The RPN Null that closes "
      "a "
      "sequence is not appended — Max does not send it either, and a patch that wants it can "
      "address parameter 16383 explicitly. Values and parameter numbers are clamped into 0-16383 "
      "rather than refused, as everywhere in this family. The output is a raw byte list for "
      "'.midiout'.");
  INLET_DOC(1, "parameter",
            "Registered parameter number to write to, stored until the next value. 0 is pitch-bend "
            "sensitivity, 1 fine tuning, 2 coarse tuning, 3 tuning program select, 4 tuning bank "
            "select; 16383 is the specification's Null, which deselects rather than writing.",
            "0-16383");
}

// ─── .nrpnout ───────────────────────────────────────────────────────────────

mNrpnOut::mNrpnOut() : mRpnOutBase(kNrpnSelectMsb, kNrpnSelectLsb) {
  ADD_DESCRIPTION(
      "Non-registered parameter number sender — Max's 'nrpnout' (issue #534). The same mechanism "
      "'.rpnout' uses, with controllers 99 and 98 selecting the 14-bit parameter number instead of "
      "101 and 100, and controllers 6 and 38 writing the 14-bit value as before. The difference is "
      "what the number means: a registered number is the MIDI specification's and means the same "
      "everywhere, while a non-registered one means whatever the device's own documentation says. "
      "That is where a hardware synthesiser's filter cutoff, envelope times and oscillator "
      "settings "
      "actually live — the parameters a patch most wants to reach and the ones no standard "
      "controller addresses — so this is the object that makes 16384 of a synth's own parameters "
      "playable from a patch. As with '.rpnout' the selection is re-sent with every value, because "
      "the receiving device has one selected parameter and anything else on the cable can move it; "
      "the four messages leave as four separate lists, one per MIDI message; and values and "
      "parameter numbers are clamped into 0-16383 rather than refused. The output is a raw byte "
      "list for '.midiout'.");
  INLET_DOC(1, "parameter",
            "Non-registered parameter number to write to, stored until the next value. What each "
            "number means is the receiving device's business — read it off the manufacturer's MIDI "
            "implementation chart.",
            "0-16383");
}
