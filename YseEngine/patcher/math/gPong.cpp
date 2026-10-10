#include "gPong.h"
#include "../pObjectList.hpp"
#include <algorithm>
#include <cmath>

using namespace YSE::PATCHER;

#define className gPong

namespace {

  // Max's @mode ordering, which .pong keeps so a patch ported from Max reads
  // the same. Only the default differs — see the class documentation.
  constexpr int MODE_NONE = 0;
  constexpr int MODE_CLIP = 1;
  constexpr int MODE_WRAP = 2;
  constexpr int MODE_FOLD = 3;

  // Carries a value around to the other side of [lo, hi). The range is
  // half-open at the top: hi itself wraps back to lo, which is what makes a
  // counter driven through this behave like a phase.
  float Wrap(float value, float lo, float span) {
    float offset = std::fmod(value - lo, span);
    // std::fmod keeps the sign of its left operand, so a value below the range
    // comes back negative and has to be lifted by one span.
    if (offset < 0.f) offset += span;
    return lo + offset;
  }

  // Reflects a value back into [lo, hi]. The reflection is periodic over twice
  // the span — a triangle wave — so a value far outside the range folds as
  // many times as it takes, exactly like Max's fold mode. Both limits are
  // fixed points, which is the difference from Wrap().
  float Fold(float value, float lo, float span) {
    const float period = span * 2.f;
    float offset = std::fmod(value - lo, period);
    if (offset < 0.f) offset += period;
    // Past the top of the range the second half of the period runs back down.
    if (offset > span) offset = period - offset;
    return lo + offset;
  }

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

  ADD_DESCRIPTION(
      "Control-rate range limiting by folding or wrapping. Keeps the value on inlet 0 inside the "
      "[low, high] range and emits it as a float. Where .clip flattens an out-of-range value "
      "against the boundary, .pong reflects it back into the range (mode 3, fold, the default) or "
      "carries it around to the other side (mode 2, wrap) — the object to reach for when "
      "generative material has to stay in a register without collapsing onto the boundary, or when "
      "a counter has to behave like a phase. Mode 1 clips and mode 0 passes the value through "
      "unchanged. Limits given the wrong way round (low above high) are treated as an ordered pair "
      "and still fold against the right two numbers, and in every mode but 0 nothing non-finite "
      "can leave the object: the result is always inside the range.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value", "Value to limit — fires the evaluation.", "any float");
  INLET_DOC(1, "low", "Lower limit — stored until the next evaluation.", "any float");
  INLET_DOC(2, "high", "Upper limit — stored until the next evaluation.", "any float");
  OUTLET_DOC(0, "out", "The input value folded, wrapped or clipped into [low, high].",
             "low to high");
  PARAM_DOC("low", "-1", "Lower limit.", "any float");
  PARAM_DOC("high", "1", "Upper limit.", "any float");
  PARAM_DOC("mode", "3", "0 none (pass through), 1 clip, 2 wrap, 3 fold.", "0 to 3");
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

  // The limits may arrive the wrong way round, so work against the ordered
  // pair rather than trusting low < high.
  const float lo = std::min(low, high);
  const float hi = std::max(low, high);
  const float span = hi - lo;

  // A collapsed range has exactly one legal answer, and it also keeps the
  // fmod() below from dividing by zero.
  if (span == 0.f) {
    outputs[0].SendFloat(lo, thread);
    return;
  }

  float value = input;
  if (std::isnan(value)) {
    // The convention ./ , .sqrt, .zmap and .clip use: substitute 0 rather than
    // propagate. The fold or wrap below then pulls that 0 into the range.
    value = 0.f;
  } else if (std::isinf(value)) {
    // An infinity has no meaningful fold or wrap — fmod would hand back a NaN
    // — so pin it to the limit it ran past, which is what .clip does with it.
    outputs[0].SendFloat(value > 0.f ? hi : lo, thread);
    return;
  }

  float result;
  switch (mode) {
  case MODE_CLIP:
    result = std::min(std::max(value, lo), hi);
    break;
  case MODE_WRAP:
    result = Wrap(value, lo, span);
    break;
  case MODE_FOLD:
  default:
    // An unrecognised mode folds: the default behaviour is the safer guess
    // than silently passing an out-of-range value through.
    result = Fold(value, lo, span);
    break;
  }

  // Rounding in fmod() can leave the result a hair outside the range for a
  // value many spans away, and the ordered clamp is two compares. Cheap
  // insurance for the promise the object makes.
  outputs[0].SendFloat(std::min(std::max(result, lo), hi), thread);
}
