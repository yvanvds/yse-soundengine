#include "dWhere.h"

using namespace YSE::PATCHER;

#define className dWhere

CONSTRUCT_DSP() {
  // in 0: the ramp to measure (signal)
  ADD_IN_0;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Reports the time since a 0-1 ramp such as a ~phasor last started its cycle, and "
                  "the time it will take to finish it, as signals in milliseconds: events at set "
                  "times within a cycle.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "phase",
            "The ramp to measure, a signal from 0 to 1, rising or falling (unconnected reads 0).",
            "0-1");
  OUTLET_DOC(0, "elapsed", "Time since the ramp last started its cycle, in ms.", "0 or higher");
  OUTLET_DOC(1, "remaining",
             "Time until the ramp ends its cycle, in ms, predicted from its current speed.",
             "0 or higher");
}

CALC() {
  // An unconnected inlet reads 0 (#1036).
  DSP::buffer& elapsed = fn(inputs[0].Signal(0.f));
  // Right outlet first, as outlets fire right to left.
  outputs[1].SendBuffer(&fn.remaining(), thread);
  outputs[0].SendBuffer(&elapsed, thread);
}

#undef className
