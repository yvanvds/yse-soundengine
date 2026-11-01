#include "dSpike.h"

using namespace YSE::PATCHER;

#define className dSpike

CONSTRUCT_DSP() {
  refractory = 0.f;

  // in 0: the input (signal, float or int); a bang restarts the refractory
  // period
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Restart);
  REG_FLOAT_IN(SetIn);
  REG_INT_IN(SetInInt);

  // in 1: the refractory period in ms
  ADD_IN_1;
  REG_FLOAT_IN(SetRefractory);
  REG_INT_IN(SetRefractoryInt);

  // out 0: the interval in ms, sent after the block (issue #1037)
  ADD_OUT_FLOAT;

  ADD_PARAM(refractory);

  ADD_DESCRIPTION("Reports the time in milliseconds between the moments a signal goes from zero "
                  "to non-zero, ignoring transitions that come sooner than a refractory period "
                  "after the last one reported.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. A bang starts "
            "the refractory period again from the next block.",
            "any float");
  INLET_DOC(1, "refractory",
            "The refractory period in milliseconds: transitions sooner than this after the last "
            "one reported are ignored.",
            "0 or more ms");
  OUTLET_DOC(0, "interval",
             "The time in milliseconds since the last reported zero to non-zero transition "
             "(the first one: since the object started). After a block with several, the "
             "last one.",
             "0 or more ms");
  PARAM_DOC("refractory", "0",
            "The refractory period in milliseconds ('~spike 50'). 0 reports every transition.",
            "0 or more ms");
}

BANG_IN(Restart) {
  (void)inlet;
  (void)thread;
  restart = true;
}

FLOAT_IN(SetIn) {
  (void)inlet;
  (void)thread;
  in = value;
}

INT_IN(SetInInt) {
  (void)inlet;
  (void)thread;
  in = static_cast<float>(value);
}

// The DSP class makes a negative or NaN period 0.
FLOAT_IN(SetRefractory) {
  (void)inlet;
  (void)thread;
  refractory = value;
}

INT_IN(SetRefractoryInt) {
  (void)inlet;
  (void)thread;
  refractory = static_cast<float>(value);
}

CALC() {
  (void)thread;
  fn.refractory(refractory.load());
  if (restart.exchange(false)) fn.restart();
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  // Never Send* from CALC(): the patcher sends this after the traversal.
  if (fn(inputs[0].Signal(in.load())) > 0) outputs[0].ArmFloat(fn.lastInterval());
}

#undef className
