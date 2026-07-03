#include "headers/defines.hpp"
#if YSE_WINDOWS
#include "mMidiPolyPressure.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className mMidiPolyPressure

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntPitch);

  ADD_IN_1;
  REG_INT_IN(SetIntPressure);

  ADD_OUT_LIST;

  ADD_PARAM(channel);

  channel = pitch = pressure = 0;

  ADD_DESCRIPTION("MIDI Polyphonic Key Pressure message generator. Inlet 0 fires the message with "
                  "the stored pressure value.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "pitch", "MIDI note number (also fires the output).", "0-127");
  INLET_DOC(1, "pressure", "Pressure value for the note (stored until next pitch).", "0-127");
  OUTLET_DOC(0, "midi", "Encoded MIDI Poly Key Pressure message.", "");
  PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntPitch) {
  pitch = (int)value;
  if (pitch < 0) pitch = 0;
  if (pitch > 127) pitch = 127;
}

INT_IN(SetIntPressure) {
  pressure = (int)value;
  if (pressure < 0) pressure = 0;
  if (pressure > 127) pressure = 127;
}

CALC() {
  std::string message = "000";
  message[0] = 0xA0 + channel;
  message[1] = pitch;
  message[2] = pressure;
  outputs[0].SendList(message, thread);
}
#endif