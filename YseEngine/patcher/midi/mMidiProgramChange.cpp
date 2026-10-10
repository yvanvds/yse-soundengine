#include "mMidiProgramChange.h"
#include "../pObjectList.hpp"
#include <algorithm>

using namespace YSE::PATCHER;

#define className mMidiProgramChange

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = cvalue = 0;

  ADD_DESCRIPTION("Builds a MIDI program-change message for .midiout. Each program number on the "
                  "inlet, 0-127 as on the wire, sends one message.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "program", "Program number as on the wire, from 0; sends the message.", "0-127");
  OUTLET_DOC(0, "midi", "The program-change message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
}

INT_IN(SetIntValue) {
  cvalue = (int)value;
  if (cvalue < 0) cvalue = 0;
  if (cvalue > 127) cvalue = 127;
}

CALC() {
  // One data byte: two bytes on the wire, no padding (#1014).
  std::string message(2, '\0');
  // A channel outside 0-15 would carry into the status nibble and make
  // another message; clamped as on .bendout (#532, #1014).
  message[0] = (char)(0xC0 + std::clamp(channel, 0, 15));
  message[1] = cvalue;
  outputs[0].SendList(message, thread);
}