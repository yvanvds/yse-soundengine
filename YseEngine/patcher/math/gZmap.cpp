#include "gZmap.h"
#include "../pObjectList.hpp"
#include "gRangeMap.h"
#include <algorithm>

using namespace YSE::PATCHER;

#define className gZmap

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

  ADD_OUT_FLOAT;

  ADD_PARAM(inLow);
  ADD_PARAM(inHigh);
  ADD_PARAM(outLow);
  ADD_PARAM(outHigh);

  // The same defaults .scale carries, so swapping one for the other is a
  // change of behaviour and not of range: the MIDI range mapped onto 0-1.
  input = 0.f;
  inLow = 0.f;
  inHigh = 127.f;
  outLow = 0.f;
  outHigh = 1.f;

  ADD_DESCRIPTION(
      "Control-rate clipped range mapping. Maps the value on inlet 0 from the input range onto the "
      "output range and emits it as a float. Unlike .scale the mapping never extrapolates: a value "
      "outside the input range is pinned to the matching output limit, which makes this the safe "
      "choice for driving a parameter that must never leave its legal range. The mapping is always "
      "linear — use .scale for an exponential curve or for extrapolation. A descending output "
      "range "
      "(low above high) clips against the correct limits, a degenerate input range (low equal to "
      "high) emits the output low, and nothing non-finite can leave the object — the result is "
      "always inside the output range.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Value to map — fires the evaluation.", "any float");
  INLET_DOC(1, "inLow", "Bottom of the input range — stored until the next evaluation.",
            "any float");
  INLET_DOC(2, "inHigh", "Top of the input range — stored until the next evaluation.", "any float");
  INLET_DOC(3, "outLow", "Bottom of the output range — stored until the next evaluation.",
            "any float");
  INLET_DOC(4, "outHigh", "Top of the output range — stored until the next evaluation.",
            "any float");
  OUTLET_DOC(0, "out", "The input value mapped onto the output range and clipped to it.",
             "any float");
  PARAM_DOC("inLow", "0", "Bottom of the input range.", "any float");
  PARAM_DOC("inHigh", "127", "Top of the input range.", "any float");
  PARAM_DOC("outLow", "0", "Bottom of the output range.", "any float");
  PARAM_DOC("outHigh", "1", "Top of the output range.", "any float");
}

void gZmap::Store(float value, int inlet) {
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
  // Pin the incoming value to the input range before mapping — that is what
  // makes this .zmap rather than .scale. Clipping the input rather than only
  // the output costs two compares and buys a sane answer for an infinity on
  // the hot inlet: it lands on the matching output limit instead of on the 0
  // MapRange substitutes for a non-finite result. The input range may be given
  // high-to-low, so clamp against the ordered pair.
  const float lo = std::min(inLow, inHigh);
  const float hi = std::max(inLow, inHigh);
  const float pinned = std::min(std::max(input, lo), hi);

  // clip = true: a NaN, which survives the clamp above, still cannot escape —
  // MapRange substitutes 0 and the clamp pulls that into the output range.
  outputs[0].SendFloat(MapRange(pinned, inLow, inHigh, outLow, outHigh, 1.f, true), thread);
}
