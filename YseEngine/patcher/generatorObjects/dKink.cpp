#include "dKink.h"

using namespace YSE::PATCHER;

#define className dKink

CONSTRUCT_DSP() {
  phase = 0.f;
  slope = 1.f;

  // in 0: the phase (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // in 1: the slope multiplier (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSlope);
  REG_INT_IN(SetSlopeInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(slope);

  ADD_DESCRIPTION("Phase distortion: bends a 0-1 ramp, such as a ~phasor, so it reaches 0.5 "
                  "faster or slower than the input, then runs on to 1 in the same cycle. Read a "
                  "cosine with it for phase distortion synthesis.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The phase to bend, a ramp from 0 to 1: a signal, or a number used while no signal "
            "is connected.",
            "0-1");
  INLET_DOC(1, "slope",
            "Slope multiplier of the rise to 0.5: 1 leaves the ramp unchanged, above 1 rises "
            "faster, below 1 slower. A signal, or a number used while no signal is connected.",
            "0 or higher");
  OUTLET_DOC(0, "out", "The bent phase.", "0-1");
  PARAM_DOC("slope", "1", "Slope multiplier used while no signal is connected.", "0 or higher");
}

FLOAT_IN(SetPhase) {
  phase = value;
}

INT_IN(SetPhaseInt) {
  phase = static_cast<float>(value);
}

FLOAT_IN(SetSlope) {
  slope = value;
}

INT_IN(SetSlopeInt) {
  slope = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(phase);
  DSP::buffer* out = inputs[1].IsConstant() ? &fn(in, slope) : &fn(in, inputs[1].Signal(slope));
  outputs[0].SendBuffer(out, thread);
}

#undef className
