#include "dChange.h"

using namespace YSE::PATCHER;

#define className dChange

CONSTRUCT_DSP() {
  floatIn = 0.f;

  // in 0: the input (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the direction signal
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Outputs the direction of a signal: 1 while it rises, -1 while it falls and 0 "
                  "while it stays the same, comparing each sample with the one before it.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in", "The input: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out",
             "1 if the input sample is above the one before it, -1 if it is below, 0 if it is "
             "the same; the sample before the first is 0.",
             "-1, 0 or 1");
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
