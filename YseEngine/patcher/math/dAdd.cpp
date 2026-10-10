#include <functional>
#include "dAdd.h"

using namespace YSE::PATCHER;

#define className dAdd

CONSTRUCT_DSP() {

  // default data
  leftIn = nullptr;
  rightIn = nullptr;
  rightFloatIn = 0.f; // the identity of addition, so a bare ~+ is neutral (#1015)

  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetLeftBuffer);

  // in 1: addend (float or audio)
  ADD_IN_1;
  REG_BUFFER_IN(SetRightBuffer);
  REG_FLOAT_IN(SetRightFloat);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Adds two audio signals, or adds a float to an audio signal.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Audio signal.", "any float");
  INLET_DOC(1, "right", "Audio signal, or a float used while no audio is connected.", "any float");
  OUTLET_DOC(0, "out", "left + right.", "any float");
  PARAM_DOC("right", "0", "Float used while no audio is connected to the right inlet.",
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

RESET() // {
leftIn = rightIn = nullptr;
}

CALC() {
  if (leftIn == nullptr) return;
  output = *leftIn;

  if (rightIn == nullptr) {
    output += rightFloatIn;
  } else {
    output += *rightIn;
  }

  outputs[0].SendBuffer(&output, thread);
}