#include "pDivide.h"

using namespace YSE::PATCHER;

#define className pDivide

CONSTRUCT_DSP() {

  // default data
  leftIn = nullptr;
  rightIn = nullptr;
  rightFloatIn = 1.f;

  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(pDivide::SetLeftBuffer);

  // in 1: multiplier (float or audio)
  ADD_IN_1;
  REG_BUFFER_IN(pDivide::SetRightBuffer);
  REG_FLOAT_IN(pDivide::SetRightFloat);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);
}

BUFFER_IN(pDivide::SetLeftBuffer) {
  leftIn = buffer;
}

BUFFER_IN(pDivide::SetRightBuffer) {
  rightIn = buffer;
}

FLOAT_IN(pDivide::SetRightFloat) {
  rightFloatIn = value;
}

RESET() // {
leftIn = rightIn = nullptr;
}

CALC() {
  output = *leftIn;

  if (rightIn == nullptr) {
    if (rightFloatIn != 0.f) output /= rightFloatIn;
  }
  else {
    output /= *rightIn;
  }

  outputs[0].SendBuffer(&output);
}