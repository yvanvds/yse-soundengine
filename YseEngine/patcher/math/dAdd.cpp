#include <functional>
#include "dAdd.h"

using namespace YSE::PATCHER;

#define className dAdd

CONSTRUCT_DSP() {

  // default data
  leftIn = nullptr;
  rightIn = nullptr;
  rightFloatIn = 1.f;
  
  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetLeftBuffer);

  // in 1: multiplier (float or audio)
  ADD_IN_1;
  REG_BUFFER_IN(SetRightBuffer);
  REG_FLOAT_IN(SetRightFloat);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);
}

BUFFER_IN(SetLeftBuffer) {
  leftIn = buffer;
}

BUFFER_IN(SetRightBuffer) {
  rightIn = buffer;
}

FLOAT_IN(SetRightFloat) {
  rightFloatIn = value;
}

RESET() // {
  leftIn = rightIn = nullptr;
}

CALC() {
  if (leftIn == nullptr) return;
  output = *leftIn;

  if (rightIn == nullptr) {
    output += rightFloatIn;
  }
  else {
    output += *rightIn;
  }

  outputs[0].SendBuffer(&output, thread);
}