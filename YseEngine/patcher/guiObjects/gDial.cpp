#include "gDial.h"
#include "../math/gRangeMap.h"
#include <cmath>
#include <limits>

using namespace YSE::PATCHER;

#define className gDial

namespace {

  // Round to the nearest whole number and land inside the int range.
  //
  // Saturating rather than answering 0 the way .expr's ExprToInt does, and for
  // MapRange's own stated reason: this object promises a number inside its
  // range, and 0 need not be inside it. A dial declared over 0-1e12 in int mode
  // should read as "as far up as an int goes", not as "the bottom".
  //
  // Halfway values round away from zero (std::round), .round's convention.
  // A NaN — only reachable from a NaN parameter, since MapRange substitutes 0
  // for anything non-finite it computes itself — takes the 0 branch, because
  // every comparison against it is false.
  int RoundToInt(float value) {
    const float rounded = std::round(value);
    if (rounded >= 2147483648.f) return std::numeric_limits<int>::max();
    if (rounded <= -2147483648.f) return std::numeric_limits<int>::min();
    if (!(rounded == rounded)) return 0;
    return (int)rounded;
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(SetBang);

  // ANY rather than FLOAT: intMode decides whether this outlet carries an int
  // or a float, and it is a creation argument — read after the outlet exists.
  ADD_OUT_ANY;

  position = 0.f;

  // The defaults have to be in place before Register() hands the fields to the
  // parameter system: a creation argument overwrites them, no argument leaves
  // them alone. 0-1 linear float is .slider's behaviour exactly.
  minimum = 0.f;
  maximum = 1.f;
  exponent = 1.f;
  intMode = 0;

  ADD_PARAM(minimum);
  ADD_PARAM(maximum);
  ADD_PARAM(exponent);
  ADD_PARAM(intMode);

  ADD_DESCRIPTION(
      "Rotary control with a range of its own. The inlet takes the knob position as a float in "
      "[0, 1], clamped on input exactly as .slider clamps, and the outlet carries that position "
      "mapped onto the minimum-maximum range — so a parameter with a real range is one object "
      "rather than a .slider and a .scale. An exponent other than 1 bends the mapping into a "
      "curve, symmetrically around the bottom of the range, which is what an audio parameter like "
      "a cutoff frequency wants. A minimum equal to the maximum emits the minimum, and any result "
      "that would not be finite emits 0. Set intMode to 1 to round the result and emit it as an "
      "int instead of a float. The GUI value is the position, not the mapped value. With the "
      "default parameters the object behaves exactly like .slider.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "position",
            "Sets the knob position and emits the mapped value; an int is widened to a float and "
            "a bang re-emits without moving the knob.",
            "0.0-1.0");
  OUTLET_DOC(0, "out", "The position mapped onto the minimum-maximum range through the exponent.",
             "minimum-maximum");
  PARAM_DOC("minimum", "0", "Value the mapping gives at position 0.", "any float");
  PARAM_DOC("maximum", "1", "Value the mapping gives at position 1.", "any float");
  PARAM_DOC("exponent", "1", "Curve exponent; 1 is a linear mapping.", "any float");
  PARAM_DOC("intMode", "0", "1 rounds the mapped value and emits an int, 0 emits a float.",
            "0 or 1");
}

void gDial::Store(float value) {
  if (value < 0.f) value = 0.f;
  if (value > 1.f) value = 1.f;
  position = value;
}

INT_IN(SetInt) {
  Store((float)value);
}

FLOAT_IN(SetFloat) {
  Store(value);
}

BANG_IN(SetBang) {}

GUI_VALUE() {
  return std::to_string(position.load());
}

CALC() {
  // Clamping is on so a parameter set that bends the curve outside the range
  // cannot send a value the object's own documentation says it will not send.
  const float mapped = MapRange(position.load(), 0.f, 1.f, minimum, maximum, exponent, true);
  if (intMode != 0) {
    outputs[0].SendInt(RoundToInt(mapped), thread);
  } else {
    outputs[0].SendFloat(mapped, thread);
  }
}
