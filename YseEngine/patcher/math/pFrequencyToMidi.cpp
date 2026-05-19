
#include "pFrequencyToMidi.h"
#include "../pObjectList.hpp"
#include "../../dsp/math.hpp"

using namespace YSE::PATCHER;

#define className pFrequencyToMidi

CONSTRUCT()
{
  frequency = 0.f;

  ADD_IN_0;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFreqInt);

  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Converts a frequency in Hz to its MIDI note number (with A4 == 69, fractional output).");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "freq", "Frequency in Hz.", "0-20000 Hz");
  OUTLET_DOC(0, "midi", "MIDI note number — fractional.", "any float");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFreqInt) {
  frequency = (float)value;
}

CALC()
{
  outputs[0].SendFloat(YSE::DSP::FreqToMidi(frequency), thread);
}
