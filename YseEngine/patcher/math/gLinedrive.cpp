#include "gLinedrive.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gLinedrive

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_2;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_3;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(inputMax);
  ADD_PARAM(outputMax);
  ADD_PARAM(curve);

  // .scale's defaults, the MIDI range mapped onto 0-1, with a 1.06 curve,
  // which suits a 0-127 controller.
  input = 0.f;
  inputMax = 127.f;
  outputMax = 1.f;
  curve = 1.06f;

  // The curve follows out = outputMax * curve^(input - inputMax). Any positive
  // curve is accepted, not only curves above 1: below 1 mirrors the response,
  // and 1 flattens it to the constant outputMax.
  ADD_DESCRIPTION("Maps a number onto an exponential curve: inputMax gives outputMax, and each "
                  "step below it divides the output by the curve. Lets a linear control, such as "
                  "a MIDI controller, drive a frequency or a gain evenly.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Number to map; sends the result.", "any float");
  INLET_DOC(1, "inputMax", "Input that gives outputMax, stored for the next result.", "any float");
  INLET_DOC(2, "outputMax", "Output at inputMax, stored for the next result.", "any float");
  INLET_DOC(3, "curve",
            "Curve base, stored for the next result. Larger is steeper; 1 gives a constant "
            "outputMax; 0 or below sends 0.",
            "greater than 0");
  OUTLET_DOC(0, "out", "The number mapped onto the curve.", "any float");
  PARAM_DOC("inputMax", "127", "Input that gives outputMax.", "any float");
  PARAM_DOC("outputMax", "1", "Output at inputMax.", "any float");
  PARAM_DOC("curve", "1.06", "Curve base. Larger is steeper; 1 gives a constant outputMax.",
            "greater than 0");
}

void gLinedrive::Store(float value, int inlet) {
  switch (inlet) {
  case 0:
    input = value;
    break;
  case 1:
    inputMax = value;
    break;
  case 2:
    outputMax = value;
    break;
  case 3:
    curve = value;
    break;
  default:
    break;
  }
}

FLOAT_IN(SetFloat) {
  Store(value, inlet);
}

INT_IN(SetInt) {
  Store((float)value, inlet);
}

CALC() {
  float result;

  // std::pow with a negative base is only defined for an integral exponent, so
  // a negative curve would hand back a NaN for almost every input and a wildly
  // signed finite value for the few that happen to land on a whole number.
  // Rejecting the whole non-positive half outright is the only way to keep the
  // curve monotonic. Written as !(curve > 0) so a NaN curve lands here too.
  if (!(curve > 0.f)) {
    result = 0.f;
  } else {
    // Max writes this as b * e^(-a log c) * e^(x log c); pow() is the same
    // expression with one rounding step instead of three.
    result = outputMax * std::pow(curve, input - inputMax);
  }

  // Reachable with an input far above the input maximum (the curve overflows),
  // or simply when a neighbouring object hands us a NaN or an infinity. The
  // convention ./ , .sqrt, .scale and .zmap use — and for the amplitude case
  // this object exists to serve, 0 is the safest value there is.
  if (!std::isfinite(result)) result = 0.f;

  outputs[0].SendFloat(result, thread);
}
