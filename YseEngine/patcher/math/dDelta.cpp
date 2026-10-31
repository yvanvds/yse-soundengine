#include "dDelta.h"

using namespace YSE::PATCHER;

#define className dDelta

CONSTRUCT_DSP() {
  floatIn = 0.f;

  // in 0: the input (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the difference signal
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Outputs the difference between each input sample and the one before it. Turns "
                  "a ramp into its slope and a step into an impulse, and marks each wrap of a "
                  "~phasor with a large negative sample.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "The input: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out",
             "The input sample minus the input sample before it; the sample before the first "
             "is 0.",
             "any float");
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  floatIn = value;
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  floatIn = static_cast<float>(value);
}

CALC() {
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&fn(inputs[0].Signal(floatIn)), thread);
}

#undef className
