#include "dEdge.h"

using namespace YSE::PATCHER;

#define className dEdge

CONSTRUCT_DSP() {
  floatIn = 0.f;

  // in 0: the input (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: zero -> non-zero; out 1: non-zero -> zero. Sent after the block
  // (issue #1037).
  ADD_OUT_BANG;
  ADD_OUT_BANG;

  ADD_DESCRIPTION("Bangs when a signal goes from zero to non-zero (left outlet) and when it goes "
                  "from non-zero to zero (right outlet).");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in", "The input: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "rise", "A bang after a block in which the input went from zero to non-zero.",
             "bang");
  OUTLET_DOC(1, "fall", "A bang after a block in which the input went from non-zero to zero.",
             "bang");
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
  (void)thread;
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  fn(inputs[0].Signal(floatIn));
  // Never Send* from CALC(): the patcher sends these after the traversal.
  if (fn.rises() > 0) outputs[0].ArmBang();
  if (fn.falls() > 0) outputs[1].ArmBang();
}

#undef className
