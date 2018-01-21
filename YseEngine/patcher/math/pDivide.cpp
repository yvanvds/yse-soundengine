#include "pDivide.h"

using namespace YSE::PATCHER;

#define className pDivide

CONSTRUCT_DSP() {

  // default data
  leftIn = nullptr;
  rightIn = nullptr;
  rightFloatIn = 1.f;

  // in 0: audio buffer
  ADD_INLET_0;
  REG_BUFFER_FUNC(pDivide::SetLeftBuffer);

  // in 1: multiplier (float or audio)
  ADD_INLET_1;
  REG_BUFFER_FUNC(pDivide::SetRightBuffer);
  REG_FLOAT_FUNC(pDivide::SetRightFloat);

  // out 0: audio output
  ADD_OUTLET_BUFFER;
}

PARAMS_FUNC() {
  if (pos == 0) rightFloatIn = value;
}

BUFFER_IN_FUNC(pDivide::SetLeftBuffer) {
  leftIn = buffer;
}

BUFFER_IN_FUNC(pDivide::SetRightBuffer) {
  rightIn = buffer;
}

FLOAT_IN_FUNC(pDivide::SetRightFloat) {
  rightFloatIn = value;
}

RESET_FUNC() // {
leftIn = rightIn = nullptr;
}

CALC_FUNC() {
  output = *leftIn;

  if (rightIn == nullptr) {
    if (rightFloatIn != 0.f) output /= rightFloatIn;
  }
  else {
    output /= *rightIn;
  }

  outputs[0].SendBuffer(&output);
}