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

  ADD_DESCRIPTION("Maps a number from the input range onto the output range, in a straight line, "
                  "and sends it as a float. A number outside the input range gives the nearest "
                  "output limit, so the result never leaves the output range.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Number to map; sends the result.", "any float");
  INLET_DOC(1, "inLow", "Input value that maps to outLow, stored for the next result.",
            "any float");
  INLET_DOC(2, "inHigh", "Input value that maps to outHigh, stored for the next result.",
            "any float");
  INLET_DOC(3, "outLow", "Output for inLow, stored for the next result.", "any float");
  INLET_DOC(4, "outHigh", "Output for inHigh, stored for the next result.", "any float");
  OUTLET_DOC(0, "out", "The number mapped onto the output range, limited to it.", "any float");
  PARAM_DOC("inLow", "0", "Input value that maps to outLow.", "any float");
  PARAM_DOC("inHigh", "127", "Input value that maps to outHigh.", "any float");
  PARAM_DOC("outLow", "0", "Output for inLow.", "any float");
  PARAM_DOC("outHigh", "1", "Output for inHigh.", "any float");
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
