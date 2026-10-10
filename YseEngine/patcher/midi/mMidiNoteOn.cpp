#include "mMidiNoteOn.h"
#include "../pObjectList.hpp"
#include <algorithm>

using namespace YSE::PATCHER;

#define className mMidiNoteOn

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntPitch);

  ADD_IN_1;
  REG_INT_IN(SetIntVelocity);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = pitch = velocity = 0;

  ADD_DESCRIPTION("Builds a MIDI note-on message for .midiout. A pitch on the left inlet sends it, "
                  "with the velocity stored on the right inlet; velocity 0 is a release.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "pitch", "Note number; sends the message.", "0-127");
  INLET_DOC(1, "velocity", "Note velocity, stored for the next pitch.", "0-127");
  OUTLET_DOC(0, "midi", "The note-on message, for .midiout.", "");
  PARAM_DOC("channel", "0", "MIDI channel, counted from 0: 0 is channel 1.", "0-15");
}

INT_IN(SetIntPitch) {
  pitch = (int)value;
  if (pitch < 0) pitch = 0;
  if (pitch > 127) pitch = 127;
}

INT_IN(SetIntVelocity) {
  velocity = (int)value;
  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;
}

CALC() {
  std::string message = "000";
  // A channel outside 0-15 would carry into the status nibble and make
  // another message; clamped as on .bendout (#532, #1014).
  message[0] = (char)(0x90 + std::clamp(channel, 0, 15));
  message[1] = pitch;
  message[2] = velocity;
  outputs[0].SendList(message, thread);
}