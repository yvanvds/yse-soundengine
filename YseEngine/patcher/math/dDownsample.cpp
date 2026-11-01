#include "dDownsample.h"

using namespace YSE::PATCHER;

#define className dDownsample

CONSTRUCT_DSP() {
  period = DSP::downsample::DEFAULT_PERIOD;

  // in 0: the signal to downsample
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the period, a number or a signal
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPeriod);
  REG_INT_IN(SetPeriodInt);

  // out 0: the downsampled signal
  ADD_OUT_BUFFER;

  ADD_PARAM(period);

  ADD_DESCRIPTION("Downsamples a signal: takes an input sample every N samples and holds it in "
                  "between, for aliased, lo-fi sounds.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "The signal to downsample (unconnected reads 0).", "any float");
  INLET_DOC(1, "period",
            "The number of samples each take is held: 1 takes every sample, 4 every fourth "
            "one; a fraction such as 2.5 takes at uneven distances with an exact average. A "
            "number or a signal read per sample (unconnected, the last number sent here or "
            "the creation argument).",
            "1 or more");
  OUTLET_DOC(0, "out", "The downsampled signal: the sample last taken.", "any float");
  PARAM_DOC("period", "1",
            "The period in samples ('~downsamp 8'). A number in the right inlet changes it, "
            "but what is saved stays the argument.",
            "1 or more");
}

// The DSP class raises a period below 1 to 1 and ignores NaN.
FLOAT_IN(SetPeriod) {
  (void)inlet;
  (void)thread;
  period = value;
}

INT_IN(SetPeriodInt) {
  (void)inlet;
  (void)thread;
  period = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets (#1036): the input reads 0, the period its number.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(period.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
