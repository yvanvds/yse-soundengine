#include "gSplit.h"
#include "../pObjectList.hpp"
#include <algorithm>

using namespace YSE::PATCHER;

#define className gSplit

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

  ADD_OUT_FLOAT; // in range
  ADD_OUT_FLOAT; // out of range

  ADD_PARAM(low);
  ADD_PARAM(high);

  // The MIDI range the object's use cases live in, and .linedrive's default
  // input range. Max leaves both arguments zero-initialised, which makes a
  // fresh object route everything but a single value to the same outlet.
  input = 0.f;
  low = 0.f;
  high = 127.f;

  ADD_DESCRIPTION(
      "Control-rate two-way routing by range. Tests the value on inlet 0 against the [low, high] "
      "range and passes it on unchanged: out of outlet 0 when it falls inside the range, out of "
      "outlet 1 when it does not. Both bounds are inclusive. Exactly one outlet fires per "
      "evaluation, which is what separates .split from the rest of the range family — .clip and "
      ".pong reshape the value, .split leaves it alone and decides where it goes. The building "
      "block for keyboard splits, velocity layers and zone-based dispatch: chain the out-of-range "
      "outlet into the next .split for the next zone. Limits given the wrong way round (low above "
      "high) are treated as an ordered pair and still split on the right two numbers. Nothing is "
      "computed and so nothing is substituted — a non-finite value is routed, and a NaN leaves the "
      "out-of-range outlet.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Value to route — fires the evaluation.", "any float");
  INLET_DOC(1, "low", "Lower limit, inclusive — stored until the next evaluation.", "any float");
  INLET_DOC(2, "high", "Upper limit, inclusive — stored until the next evaluation.", "any float");
  OUTLET_DOC(0, "in", "The input value, unchanged, when it lies inside [low, high].",
             "low to high");
  OUTLET_DOC(1, "out", "The input value, unchanged, when it lies outside [low, high].",
             "any float");
  PARAM_DOC("low", "0", "Lower limit, inclusive.", "any float");
  PARAM_DOC("high", "127", "Upper limit, inclusive.", "any float");
}

void gSplit::Store(float value, int inlet) {
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
  // The limits may arrive the wrong way round, so test against the ordered
  // pair rather than trusting low < high. A NaN limit survives both std::min
  // and std::max, which is harmless: the comparisons below are false either
  // way and the value takes the out-of-range branch.
  const float lo = std::min(low, high);
  const float hi = std::max(low, high);

  // Both bounds inclusive, as in Max. A NaN input fails both compares and so
  // lands on outlet 1 — a value that cannot be shown to be inside the range is
  // outside it. Exactly one outlet fires, so there is no right-to-left firing
  // order to respect here.
  if (input >= lo && input <= hi) {
    outputs[0].SendFloat(input, thread);
  } else {
    outputs[1].SendFloat(input, thread);
  }
}
