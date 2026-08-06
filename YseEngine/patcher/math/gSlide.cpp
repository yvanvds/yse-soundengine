#include "gSlide.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <algorithm>
#include <cmath>
#include <string>

using namespace YSE::PATCHER;

#define className gSlide

namespace {

  // How close to the target counts as having arrived, relative to the target
  // and never coarser than this in absolute terms. See "Convergence" in the
  // header: the recursion only approaches its target, and in floating point it
  // stops moving a hair short of it, so the arrival has to be defined rather
  // than waited for. 1e-6 is about eight ulps of the float the outlet carries,
  // so the snap is invisible to anything downstream.
  constexpr double SNAP_TOLERANCE = 1e-6;

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_2;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(slideUp);
  ADD_PARAM(slideDown);

  // Max's defaults. 1 is the pass-through case, so an unconfigured .slide is
  // transparent and only starts smoothing once it is told how much to.
  input = 0.f;
  slideUp = 1.f;
  slideDown = 1.f;
  current = 0.0;

  ADD_DESCRIPTION(
      "Logarithmic value smoothing — a one-pole filter on control values, following Max's "
      "y[n] = y[n-1] + (x[n] - y[n-1]) / slide. Each number received moves the running value a "
      "fraction of the way towards itself instead of replacing it, which is what turns stepped "
      "MIDI controller or envelope-follower data into a curve and keeps the parameter it drives "
      "from zippering. The two amounts are independent: slideUp applies when the incoming value "
      "is above the running one and slideDown when it is below, so a fast rise with a slow fall "
      "is one object rather than two. A slide of 1 passes the input straight through and larger "
      "amounts change proportionally more slowly, so 10 moves a tenth as fast; the parameter "
      "counts incoming values, not seconds, since the object is clocked by events rather than by "
      "the sample rate (use ~line for a linear ramp over a stated duration). An amount below 1 "
      "would make the recursion overshoot and ring, so anything below 1 — including 0 and "
      "negatives — is treated as 1. Once the running value is within one part in a million of the "
      "target it snaps onto it exactly, so the smoother settles on its input rather than stalling "
      "a rounding error short of it. A bang re-runs the filter on the last value received, 'set "
      "<n>' stages a value without emitting and 'reset' returns the running value to 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value",
            "Value to smooth — fires one filter step / bang repeats the step on the last value / "
            "list 'set <n>' stages a value without emitting / list 'reset' zeroes the running "
            "value.",
            "any float");
  INLET_DOC(1, "slideUp", "Slide amount used while the input is above the running value.", "1+");
  INLET_DOC(2, "slideDown", "Slide amount used while the input is below the running value.", "1+");
  OUTLET_DOC(0, "out", "The smoothed value.", "between the previous output and the input");
  PARAM_DOC("slideUp", "1",
            "Slide amount used while the input is above the running value; below 1 "
            "reads as 1, which passes the input through.",
            "1+");
  PARAM_DOC("slideDown", "1",
            "Slide amount used while the input is below the running value; below 1 "
            "reads as 1, which passes the input through.",
            "1+");
}

double gSlide::Effective(float slide) {
  // Max documents 1 and 10 and nothing else. Below 1 the error is multiplied
  // by more than one per step, so the value overshoots the target and then
  // oscillates around it — the opposite of smoothing — and 0 divides by zero.
  // A NaN fails this comparison and therefore also reads as 1, which is the
  // answer we want for it anyway.
  return (slide > 1.f) ? static_cast<double>(slide) : 1.0;
}

void gSlide::Slide(YSE::THREAD thread) {
  // A non-finite input has to be caught before it reaches the running value:
  // an infinity would pin the state at infinity and the *next* finite input
  // would then compute inf + (x - inf) = NaN, poisoning the object for good.
  // Substituting 0 is the convention ./ , .sqrt, .zmap and .clip already use.
  const double target = std::isfinite(input) ? static_cast<double>(input) : 0.0;
  const double from = current;

  // Which of the two amounts applies is decided per step by the direction of
  // travel, which is the whole point of having two of them.
  const double slide = Effective(target > from ? slideUp : slideDown);

  // The step itself. `slide` is at least 1, so `next` always lies between
  // `from` and `target` — the output never overshoots.
  double next = from + ((target - from) / slide);

  // Arrival, defined rather than waited for. Scaling the tolerance by the
  // target keeps it meaningful for a 20000 Hz value as well as a 0-1 one; the
  // floor of 1 keeps a target of 0 (or any small one) terminating instead of
  // demanding an exactness float cannot deliver.
  const double tolerance = SNAP_TOLERANCE * std::max(1.0, std::fabs(target));
  if (std::fabs(target - next) <= tolerance) next = target;

  current = next;
  outputs[0].SendFloat(static_cast<float>(next), thread);
}

void gSlide::Store(float value, int inlet, YSE::THREAD thread) {
  switch (inlet) {
  case 0:
    input = value;
    Slide(thread);
    break;
  case 1:
    slideUp = value;
    break;
  case 2:
    slideDown = value;
    break;
  default:
    break;
  }
}

FLOAT_IN(SetFloat) {
  Store(value, inlet, thread);
}

INT_IN(SetInt) {
  // Max converts an int to a float here; the object is a float object.
  Store(static_cast<float>(value), inlet, thread);
}

BANG_IN(Bang) {
  if (inlet != 0) return;
  // "Performs the same function as float using the last input value." The
  // running value has moved since that input arrived, so a bang walks the
  // output one more step towards it.
  Slide(thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  if (value == "reset") {
    // Max: "Resets the current output sample to 0." The staged input is left
    // alone, so the next bang slides from 0 back towards it.
    current = 0.0;
    return;
  }

  if (value.compare(0, 4, "set ") == 0) {
    // Max: "set the current input value to the given number without causing
    // output (bang can be used to cause successive output)". So `set n` plus a
    // bang is exactly the float `n` that was not sent — it stages the input,
    // where `reset` loads the running value.
    float argument = 0.f;
    if (ExprParseFloatList(value.c_str() + 4, &argument, 1) == 1) input = argument;
    return;
  }

  // Anything else is not a message this object knows. Ignored rather than
  // guessed at.
}

GUI_VALUE() {
  return std::to_string(current);
}
