#include "headers/defines.hpp"
// See the matching guard in mMidiBendOut.h.
#if YSE_WINDOWS
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

  ADD_DESCRIPTION(
      "MIDI Pitch Bend message generator, the expressive channel message the other senders "
      "in this family do not cover. A value on the inlet stores it and emits a three-byte "
      "packet on the channel given as the creation argument. The value is the coarse byte "
      "of the wire message, 0-127 with 64 at rest, and the fine byte is sent as 0: that is "
      "the 7-bit resolution Max's 'bendout' has and the one '.bendin', '.midiparse' and "
      "'.midiformat' report, so a number means the same thing wherever a patch reads or "
      "writes it. The full 14-bit form is '.xbendout'. Out-of-range values are clamped "
      "rather than refused, as they are on the rest of the family, so a patch scaling a "
      "controller into a wider range bends to the extremes instead of falling silent. The "
      "output is a raw byte list for '.midiout'.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "bend", "Pitch bend position, 64 at rest (also fires the output).", "0-127");
  OUTLET_DOC(0, "midi", "Encoded MIDI Pitch Bend message (status, fine byte 0, bend).", "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
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
#endif
