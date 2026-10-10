#include "gScale.h"
#include "../pObjectList.hpp"
#include "gRangeMap.h"

using namespace YSE::PATCHER;

#define className gScale

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

  ADD_IN_4;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_5;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(inLow);
  ADD_PARAM(inHigh);
  ADD_PARAM(outLow);
  ADD_PARAM(outHigh);
  ADD_PARAM(exponent);
  ADD_PARAM(clip);

  // Max's defaults: the MIDI range mapped onto 0-1, linear, extrapolating.
  input = 0.f;
  inLow = 0.f;
  inHigh = 127.f;
  outLow = 0.f;
  outHigh = 1.f;
  exponent = 1.f;
  clip = 0;

  ADD_DESCRIPTION(
      "Control-rate range mapping. Maps the value on inlet 0 from the input range onto the output "
      "range and emits it as a float. An exponent other than 1 bends the mapping into a curve, "
      "symmetrically around the bottom of the input range. Like Max's default the mapping "
      "extrapolates: an input outside the input range gives an output outside the output range. "
      "Set the clip parameter to 1 to clamp the result to the output range instead. A degenerate "
      "input range (low equal to high) emits the output low, and any result that would not be "
      "finite emits 0 rather than a NaN or an infinity.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Value to map — fires the evaluation.", "any float");
  INLET_DOC(1, "inLow", "Bottom of the input range — stored until the next evaluation.",
            "any float");
  INLET_DOC(2, "inHigh", "Top of the input range — stored until the next evaluation.", "any float");
  INLET_DOC(3, "outLow", "Bottom of the output range — stored until the next evaluation.",
            "any float");
  INLET_DOC(4, "outHigh", "Top of the output range — stored until the next evaluation.",
            "any float");
  INLET_DOC(5, "exponent",
            "Curve exponent, 1 for a linear mapping — stored until the next evaluation.",
            "any float");
  OUTLET_DOC(0, "out", "The input value mapped onto the output range.", "any float");
  PARAM_DOC("inLow", "0", "Bottom of the input range.", "any float");
  PARAM_DOC("inHigh", "127", "Top of the input range.", "any float");
  PARAM_DOC("outLow", "0", "Bottom of the output range.", "any float");
  PARAM_DOC("outHigh", "1", "Top of the output range.", "any float");
  PARAM_DOC("exponent", "1", "Curve exponent; 1 is a linear mapping.", "any float");
  PARAM_DOC("clip", "0",
            "1 clamps the result to the output range, 0 lets it extrapolate (Max's default).",
            "0 or 1");
}

void gScale::Store(float value, int inlet) {
  switch (inlet) {
  case 0:
    input = value;
    break;
  case 1:
    inLow = value;
    break;
  case 2:
    inHigh = value;
    break;
  case 3:
    outLow = value;
    break;
  case 4:
    outHigh = value;
    break;
  case 5:
    exponent = value;
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
  // The whole mapping — the degenerate-range guard, the mirrored exponent, the
  // non-finite substitution and the ordered clamp — lives in MapRange, which
  // .zmap (#444) shares. A divide, at most one libm call and one Send: no
  // allocation, no lock, no I/O.
  outputs[0].SendFloat(MapRange(input, inLow, inHigh, outLow, outHigh, exponent, clip != 0),
                       thread);
}
