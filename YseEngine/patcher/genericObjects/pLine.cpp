#include "pLine.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className pLine

CONSTRUCT_DSP() {
  // in 0: target value (float)
  // in 1: time in milliseconds (int)

  // out 0: dsp buffer

  ADD_IN_0;
  REG_FLOAT_IN(SetTarget);

  ADD_IN_1;
  REG_FLOAT_IN(SetTime);

  ADD_OUT_BUFFER;

  ADD_PARAM(target);
  ADD_PARAM(time);

  stop = false;

  ADD_DESCRIPTION("Linear-ramp generator. Ramps an audio-rate value toward a target over a given "
                  "time in ms. The 'stop' message freezes the ramp at the current value.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "target", "Target value to ramp toward.", "any float");
  INLET_DOC(1, "time", "Ramp duration in milliseconds.", "0+ ms");
  OUTLET_DOC(0, "out", "Audio-rate ramp output.", "any float");
  PARAM_DOC("target", "0", "Initial target value.", "any float");
  PARAM_DOC("time", "0", "Initial ramp time in milliseconds.", "0+ ms");
}

MESSAGES() {
  if (message == "stop") {
    stop = true;
  }
}

FLOAT_IN(SetTarget) {
  target = value;
}

FLOAT_IN(SetTime) {
  time = value;
}

CALC() {
  if (stop) {
    ramp.stop();
    stop = false;
  } else {
    ramp.setIfNew(target, (int)time);
  }
  ramp.update();
  outputs[0].SendBuffer(&ramp(), thread);
}