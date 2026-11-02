#include "gPong.h"
#include "../pObjectList.hpp"
#include "../../dsp/operators/fold.hpp"

using namespace YSE::PATCHER;

#define className gPong

namespace {

  // Max's @mode ordering, which .pong keeps so a patch ported from Max reads
  // the same. Only the default differs — see the class documentation.
  constexpr int MODE_NONE = 0;
  constexpr int MODE_CLIP = 1;
  constexpr int MODE_WRAP = 2;
  constexpr int MODE_FOLD = 3;

} // namespace

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
  ADD_PARAM(mode);

  // .clip's (and ~clip's) range, so the two range limiters behave alike out of
  // the box, and the folding this object is named for rather than Max's
  // pass-through default.
  input = 0.f;
  low = -1.f;
  high = 1.f;
  mode = MODE_FOLD;

  ADD_DESCRIPTION("Keeps a number inside the range low to high by folding it back from the "
                  "limits, the default, by wrapping it around to the other side, or by clipping "
                  "it. Sends the result as a float.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Number to keep in range; sends the result.", "any float");
  INLET_DOC(1, "low", "Lower limit, stored for the next result.", "any float");
  INLET_DOC(2, "high", "Upper limit, stored for the next result.", "any float");
  OUTLET_DOC(0, "out", "The number folded, wrapped or clipped into the range.", "low to high");
  PARAM_DOC("low", "-1", "Lower limit.", "any float");
  PARAM_DOC("high", "1", "Upper limit.", "any float");
  PARAM_DOC("mode", "3", "0 passes numbers through, 1 clips, 2 wraps, 3 folds.", "0 to 3");
}

void gPong::Store(float value, int inlet) {
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
  // Mode 0 is Max's pass-through: no range, no guards, nothing to compute.
  if (mode == MODE_NONE) {
    outputs[0].SendFloat(input, thread);
    return;
  }

  // The range rules (ordered limits, NaN as 0, an infinity pinned to its
  // limit, a result always in range) are DSP::fold's, shared with ~pong.
  using MODE = YSE::DSP::fold::MODE;
  const MODE how = mode == MODE_CLIP ? MODE::CLIP : (mode == MODE_WRAP ? MODE::WRAP : MODE::FOLD);
  // An unrecognised mode folds: the default behaviour is the safer guess than
  // silently passing an out-of-range value through.
  outputs[0].SendFloat(YSE::DSP::fold::Limit(input, low, high, how), thread);
}
