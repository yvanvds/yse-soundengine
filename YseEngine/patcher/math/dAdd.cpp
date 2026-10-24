#include "dAdd.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dAdd

CONSTRUCT_DSP() {

  // default data
  leftFloatIn = 0.f;
  rightFloatIn = 0.f; // the identity of addition, so a bare ~+ is neutral (#1015)

  // in 0: augend (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: addend (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Adds two audio signals, or adds a number to an audio signal.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Audio signal, or a number used while no audio is connected.", "any float");
  INLET_DOC(1, "right", "Audio signal, or a number used while no audio is connected.", "any float");
  OUTLET_DOC(0, "out", "left + right.", "any float");
  PARAM_DOC("right", "0", "Number used while no audio is connected to the right inlet.",
            "any float");
}

FLOAT_IN(SetLeftFloat) {
  leftFloatIn = value;
}

INT_IN(SetLeftInt) {
  leftFloatIn = static_cast<float>(value);
}

FLOAT_IN(SetRightFloat) {
  rightFloatIn = value;
}

INT_IN(SetRightInt) {
  rightFloatIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  output = inputs[0].Signal(leftFloatIn);

  if (inputs[1].IsConstant()) {
    output += rightFloatIn;
  } else {
    output += inputs[1].Signal(rightFloatIn);
  }

  outputs[0].SendBuffer(&output, thread);
}

#undef className