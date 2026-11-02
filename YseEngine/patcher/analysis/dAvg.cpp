#include "dAvg.h"

using namespace YSE::PATCHER;

#define className dAvg

CONSTRUCT_DSP() {
  fn.mode(DSP::movingAverage::ABSOLUTE);

  // in 0: the signal; a bang reports
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);

  // out 0: the average, sent after the block (issue #1037)
  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Reports the average absolute value of a signal since the last bang: cheap "
                  "level polling.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to measure; unconnected it reads as silence. A bang reports the "
            "average so far and starts again.",
            "any float");
  OUTLET_DOC(0, "average",
             "The mean absolute sample value since the last bang. 0 when nothing was "
             "measured.",
             "0 or more");
}

BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  report = true;
}

CALC() {
  (void)thread;
  // Never Send* from CALC(): the patcher sends this after the traversal.
  // A bang reports the average up to the end of the last block.
  if (report.exchange(false)) outputs[0].ArmFloat(fn.take());
  // Unconnected, the inlet reads as silence (#1036).
  fn.accumulate(inputs[0].Signal(0.f));
}

#undef className
