#include "pLine.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className pLine

CONSTRUCT_DSP()
{
  // in 0: target value (float)
  // in 1: time in milliseconds (int)

  // out 0: dsp buffer

  ADD_IN_0;
  REG_FLOAT_IN(pLine::SetTarget);

  ADD_IN_1;
  REG_FLOAT_IN(pLine::SetTime);
  
  ADD_OUT_BUFFER;

  ADD_PARAM(target);
  ADD_PARAM(time);

  stop = false;
}

MESSAGES() {
  if (message == "stop") {
    stop = true;
  }
}

FLOAT_IN(pLine::SetTarget) {
  target = value;
}

FLOAT_IN(pLine::SetTime) {
  time = value;
}

CALC() {
  if (stop) {
    ramp.stop();
    stop = false;
  }
  else {
    ramp.setIfNew(target, time);
  }
  ramp.update();
  outputs[0].SendBuffer(&ramp());
}