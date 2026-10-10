#include "dMultiply.h"

using namespace YSE::PATCHER;

#define className dMultiply

CONSTRUCT_DSP() {

  // default data
  leftIn = nullptr;
  rightIn = nullptr;
  rightFloatIn = 1.f;

  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetLeftBuffer);

  // in 1: multiplier (int, float or audio)
  ADD_IN_1;
  REG_BUFFER_IN(SetRightBuffer);
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Multiplies an audio signal by another audio signal or by a float: a gain "
                  "stage, or a ring modulator.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Audio signal.", "any float");
  INLET_DOC(1, "right", "Audio signal, or a number used while no audio is connected.", "any float");
  OUTLET_DOC(0, "out", "left * right.", "any float");
  PARAM_DOC("right", "1.0", "Float used while no audio is connected to the right inlet.",
            "any float");
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

INT_IN(SetRightInt) {
  rightFloatIn = static_cast<float>(value);
}

RESET() // {
leftIn = rightIn = nullptr;
}

CALC() {
  if (leftIn == nullptr) return;

  output = *leftIn;

  if (rightIn == nullptr) {
    output *= rightFloatIn;
  } else {
    output *= *rightIn;
  }

  outputs[0].SendBuffer(&output, thread);
}