#include "dSaw.h"

using namespace YSE::PATCHER;

#define className dSaw

CONSTRUCT_DSP() {
  frequency = 440.f;

  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  ADD_DESCRIPTION("Audio-rate sawtooth oscillator. Frequency can be set with a float or modulated "
                  "with a DSP buffer.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq", "Oscillator frequency in Hz. Accepts a DSP buffer (FM) or a float.",
            "0-20000 Hz");
  OUTLET_DOC(0, "out", "Sawtooth wave audio output.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "440", "Initial frequency in Hz.", "0-20000 Hz");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

CALC() {
  // DSP::saw is a phasor (0..1). Rescale it in place to a bipolar -1..1 saw.
  // A connected frequency signal wins over the float (issue #1036).
  DSP::buffer& out =
      inputs[0].IsConstant() ? saw(frequency) : saw(inputs[0].Signal(frequency));
  Flt* ptr = out.getPtr();
  for (UInt i = 0; i < out.getLength(); ++i)
    ptr[i] = 2.f * ptr[i] - 1.f;
  outputs[0].SendBuffer(&out, thread);
}
