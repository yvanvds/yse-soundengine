#include "mMidiBendOut.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className mMidiBendOut

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = 0;
  // 64 is the pitch wheel at rest, which is the honest resting value for the
  // field even though the inlet overwrites it before every send.
  cvalue = 64;

  ADD_DESCRIPTION("Builds a 7-bit MIDI pitch-bend message for .midiout: 0-127 with 64 at rest. "
                  ".xbendout sends the full 14 bits.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "bend", "Pitch bend, 64 at rest; sends the message.", "0-127");
  OUTLET_DOC(0, "midi", "The pitch-bend message, for .midiout. The fine byte is 0.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
}

INT_IN(SetIntValue) {
  cvalue = (int)value;
  if (cvalue < 0) cvalue = 0;
  if (cvalue > 127) cvalue = 127;
}

CALC() {
  // A channel outside 0-15 would carry into the status nibble and make the
  // packet a different message altogether — a bend arriving as a note-off is
  // worse than a bend on the wrong channel — so it is clamped rather than
  // added in raw. Clamping into the range MIDI has, rather than refusing, is
  // what the rest of this family does with its data bytes.
  int ch = channel;
  if (ch < 0) ch = 0;
  if (ch > 15) ch = 15;

  // Three chars: short-string storage, so nothing is allocated here.
  std::string message = "000";
  message[0] = (char)(0xE0 + ch);
  // The wire order is fine byte then coarse. This is the 7-bit half of the
  // pair, so the fine byte is 0 and the value carries in the coarse one.
  message[1] = 0;
  message[2] = (char)cvalue;
  outputs[0].SendList(message, thread);
}
