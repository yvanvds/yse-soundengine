#include "dReverseSubstract.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dReverseSubstract

CONSTRUCT_DSP() {
  leftFloatIn = 0.f;
  rightFloatIn = 0.f;

  // in 0: the value subtracted (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: the value subtracted from (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: right - left
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Subtracts the left signal from the right one: ~- with the inlets reversed. With "
                  "a right number of 1 it turns a 0-1 envelope or ramp upside down.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "The value subtracted: a signal, or a number used while no signal is connected.",
            "any float");
  INLET_DOC(1, "right",
            "The value subtracted from: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out", "right - left.", "any float");
  PARAM_DOC("right", "0", "Number used while no signal is connected to the right inlet.",
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
  DSP::buffer& left = inputs[0].Signal(leftFloatIn);
  DSP::buffer& out =
      inputs[1].IsConstant() ? op(left, rightFloatIn) : op(left, inputs[1].Signal(rightFloatIn));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
