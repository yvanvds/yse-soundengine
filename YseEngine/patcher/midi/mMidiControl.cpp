#include "mMidiControl.h"
#include "../pObjectList.hpp"
#include <algorithm>

using namespace YSE::PATCHER;

#define className mMidiControl

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);

  ADD_OUT_LIST;

  ADD_PARAM(channel);
  ADD_PARAM(controller);

  channel = controller = cvalue = 0;

  ADD_DESCRIPTION("Builds a MIDI control-change message for .midiout. Each value on the inlet "
                  "sends one message, on the controller and channel the arguments set.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "value", "Controller value; sends the message.", "0-127");
  OUTLET_DOC(0, "midi", "The control-change message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
  PARAM_DOC("controller", "0", "Controller number.", "0-127");
}

INT_IN(SetIntValue) {
  cvalue = (int)value;
  if (cvalue < 0) cvalue = 0;
  if (cvalue > 127) cvalue = 127;
}

CALC() {
  std::string message = "000";
  // A channel outside 0-15 would carry into the status nibble and make
  // another message; clamped as on .bendout (#532, #1014).
  message[0] = (char)(0xB0 + std::clamp(channel, 0, 15));
  message[1] = controller;
  message[2] = cvalue;
  outputs[0].SendList(message, thread);
}