#include "dDivide.h"

using namespace YSE::PATCHER;

#define className dDivide

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

  ADD_DESCRIPTION(
      "Audio-rate divide. Divides the left buffer by either the right buffer (audio-rate) or the "
      "right float (control-rate). A right-float of 0 is silently treated as a no-op.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Left operand — audio buffer.", "any float");
  INLET_DOC(1, "right", "Right operand — audio buffer or float (non-zero).", "any non-zero float");
  OUTLET_DOC(0, "out", "left / right.", "any float");
  PARAM_DOC(
      "right", "1.0",
      "Initial right-operand float (used until a buffer arrives on inlet 1). 0 disables division.",
      "any non-zero float");
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
    if (rightFloatIn != 0.f) output /= rightFloatIn;
  } else {
    output /= *rightIn;
  }

  outputs[0].SendBuffer(&output, thread);
}