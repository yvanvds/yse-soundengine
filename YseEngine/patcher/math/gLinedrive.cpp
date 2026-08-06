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

  // .scale's defaults — the MIDI range mapped onto 0-1 — with the 1.06 curve
  // Max's own documentation calls appropriate for a 0-127 controller.
  input = 0.f;
  inputMax = 127.f;
  outputMax = 1.f;
  curve = 1.06f;

  ADD_DESCRIPTION(
      "Exponential scaling for control values. Maps the value on inlet 0 onto an exponential "
      "response curve and emits it as a float, following Max's formula: out = outputMax * "
      "curve^(input - inputMax). The input maximum is the anchor — feed it in and the output is "
      "exactly outputMax, whatever the curve — and below it the output falls away geometrically, "
      "one step of input always multiplying the output by curve. That is what makes a linear "
      "controller such as a MIDI CC or a slider feel even across its whole range when it drives a "
      "frequency or an amplitude, where .scale's linear mapping would bunch the audible change up "
      "at one end. Max requires a curve above 1; any positive curve is accepted here, one below 1 "
      "mirroring the response and 1 flattening it to the constant outputMax. A curve at or below "
      "0 has no real exponential and emits 0, as does any result that would not be finite.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Value to scale — fires the evaluation.", "any float");
  INLET_DOC(1, "inputMax",
            "Input value that maps onto the output maximum — stored until the next evaluation.",
            "any float");
  INLET_DOC(2, "outputMax",
            "Output value reached at the input maximum — stored until the next evaluation.",
            "any float");
  INLET_DOC(3, "curve",
            "Curve base; larger is more steeply exponential, 1 is a constant — stored until the "
            "next evaluation.",
            "greater than 0");
  OUTLET_DOC(0, "out", "The input value scaled along the exponential curve.", "any float");
  PARAM_DOC("inputMax", "127", "Input value that maps onto the output maximum.", "any float");
  PARAM_DOC("outputMax", "1", "Output value reached at the input maximum.", "any float");
  PARAM_DOC("curve", "1.06", "Curve base; larger is more steeply exponential, 1 is a constant.",
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
