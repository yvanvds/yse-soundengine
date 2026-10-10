
#include "pMidiToFrequency.h"
#include "../pObjectList.hpp"
#include "../../dsp/math.hpp"

using namespace YSE::PATCHER;

#define className pMidiToFrequency

CONSTRUCT() {

  midiValue = 0.f;

  ADD_IN_0;
  REG_FLOAT_IN(SetMidi);
  REG_INT_IN(SetMidiInt);

  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Converts a MIDI note number to a frequency in Hz, with note 69 (A4) at 440 Hz. "
                  "Fractional note numbers give the frequencies in between.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "midi", "MIDI note number, fractional values included; sends the result.", "0-127");
  OUTLET_DOC(0, "freq", "Frequency in Hz.", "8.18-12543 Hz");
}

FLOAT_IN(SetMidi) {
  midiValue = value;
}

INT_IN(SetMidiInt) {
  midiValue = (float)value;
}

CALC() {
  outputs[0].SendFloat(YSE::DSP::MidiToFreq(midiValue), thread);
}
