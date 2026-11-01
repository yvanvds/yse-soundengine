#include "dDegrade.h"

using namespace YSE::PATCHER;

#define className dDegrade

CONSTRUCT_DSP() {
  ratio = DSP::degrade::DEFAULT_RATIO;
  bits = static_cast<float>(DSP::degrade::DEFAULT_BITS);

  // in 0: the signal to degrade
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the ratio, a number or a signal
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRatio);
  REG_INT_IN(SetRatioInt);

  // in 2: the bits, a number or a signal
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetBits);
  REG_INT_IN(SetBitsInt);

  // out 0: the degraded signal
  ADD_OUT_BUFFER;

  ADD_PARAM(ratio);
  ADD_PARAM(bits);

  ADD_DESCRIPTION("A bitcrusher: holds the signal at a fraction of the sample rate and quantizes "
                  "it to a number of bits, for lo-fi, aliased and gritty sounds.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "The signal to degrade (unconnected reads 0).", "any float");
  INLET_DOC(1, "ratio",
            "The fraction of the sample rate at which a new input sample is taken; between two "
            "takes the last one is held. 1 takes every sample, 0.5 every second one, 0 holds "
            "for good. A number or a signal read per sample (unconnected, the last number sent "
            "here or the creation argument).",
            "0 to 1");
  INLET_DOC(2, "bits",
            "The number of bits the output is quantized to; a fraction is truncated. A number "
            "or a signal read per sample (unconnected, the last number sent here or the "
            "creation argument).",
            "1 to 24");
  OUTLET_DOC(0, "out",
             "The degraded signal: the held sample, rounded down to a step of 2 to the power of "
             "(1 - bits).",
             "any float");
  PARAM_DOC("ratio", "1",
            "The sample rate ratio ('~degrade 0.25'). A number in the middle inlet changes it, "
            "but what is saved stays the argument.",
            "0 to 1");
  PARAM_DOC("bits", "24",
            "The number of bits ('~degrade 0.25 8'). A number in the right inlet changes it, "
            "but what is saved stays the argument.",
            "1 to 24");
}

// The DSP class clamps the ratio to 0..1 and the bits to 1..24 and ignores
// NaN.
FLOAT_IN(SetRatio) {
  (void)inlet;
  (void)thread;
  ratio = value;
}

INT_IN(SetRatioInt) {
  (void)inlet;
  (void)thread;
  ratio = static_cast<float>(value);
}

FLOAT_IN(SetBits) {
  (void)inlet;
  (void)thread;
  bits = value;
}

INT_IN(SetBitsInt) {
  (void)inlet;
  (void)thread;
  bits = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets (#1036): the input reads 0, the ratio and the bits
  // their numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(ratio.load()), inputs[2].Signal(bits.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
