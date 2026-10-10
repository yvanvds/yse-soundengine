
#include "pFrequencyToMidi.h"
#include "../pObjectList.hpp"
#include "../../dsp/math.hpp"

using namespace YSE::PATCHER;

#define className pFrequencyToMidi

CONSTRUCT() {
  frequency = 0.f;

  ADD_IN_0;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFreqInt);

  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Converts a frequency in Hz to a MIDI note number, with 440 Hz (A4) at note 69. "
                  "The result is fractional, so a detuned pitch keeps its offset.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "freq", "Frequency in Hz; sends the result. 0 Hz or below gives -1500.",
            "0-20000 Hz");
  OUTLET_DOC(0, "midi", "MIDI note number, with a fractional part.", "any float");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFreqInt) {
  frequency = (float)value;
}

CALC() {
  outputs[0].SendFloat(YSE::DSP::FreqToMidi(frequency), thread);
}
