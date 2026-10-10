#include "gClip.h"
#include "../pObjectList.hpp"
#include <algorithm>
#include <cmath>

using namespace YSE::PATCHER;

#define className gClip

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

  ADD_OUT_FLOAT;

  ADD_PARAM(low);
  ADD_PARAM(high);

  // The defaults ~clip carries, so swapping the control-rate object for the
  // audio-rate one is a change of rate and not of range.
  input = 0.f;
  low = -1.f;
  high = 1.f;

  ADD_DESCRIPTION("Limits a number to the range low to high and sends it as a float. A number "
                  "inside the range passes unchanged; any other becomes the nearest limit.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Number to limit; sends the result.", "any float");
  INLET_DOC(1, "low", "Lower limit, stored for the next result.", "any float");
  INLET_DOC(2, "high", "Upper limit, stored for the next result.", "any float");
  OUTLET_DOC(0, "out", "The number limited to the range.", "low to high");
  PARAM_DOC("low", "-1", "Lower limit.", "any float");
  PARAM_DOC("high", "1", "Upper limit.", "any float");
}

void gClip::Store(float value, int inlet) {
  switch (inlet) {
  case 0:
    input = value;
    break;
  case 1:
    low = value;
    break;
  case 2:
    high = value;
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
  // The limits may arrive the wrong way round, so clamp against the ordered
  // pair rather than trusting low < high.
  const float lo = std::min(low, high);
  const float hi = std::max(low, high);

  // A NaN survives std::min/std::max untouched (every comparison against it is
  // false), so it has to be caught explicitly or it would escape the range the
  // object promises. Substituting 0 is what ./ , .sqrt and .zmap do; the clamp
  // below then pulls that 0 into the range. An infinity needs no special case —
  // it simply clips to the matching limit.
  const float value = std::isnan(input) ? 0.f : input;

  outputs[0].SendFloat(std::min(std::max(value, lo), hi), thread);
}
