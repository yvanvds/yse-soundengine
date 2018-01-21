#include "pLine.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className pLine

CONSTRUCT_DSP()
{
  // in 0: target value (float)
  // in 1: time in milliseconds (int)

  // out 0: dsp buffer

  ADD_INLET_0;
  REG_FLOAT_FUNC(pLine::SetTarget);

  ADD_INLET_1;
  REG_FLOAT_FUNC(pLine::SetTime);
  
  ADD_OUTLET_BUFFER;

  stop = false;
}

PARAMS_FUNC() {
  switch (pos) {
    case 0: target = value; break;
    case 1: time = value; break;
  }
}

MESSAGES_FUNC() {
  if (message == "stop") {
    stop = true;
  }
}

FLOAT_IN_FUNC(pLine::SetTarget) {
  target = value;
}

FLOAT_IN_FUNC(pLine::SetTime) {
  time = value;
}

CALC_FUNC() {
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