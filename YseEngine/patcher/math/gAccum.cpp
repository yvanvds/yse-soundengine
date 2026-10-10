#include "gAccum.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <cmath>
#include <string>

using namespace YSE::PATCHER;

#define className gAccum

namespace {

  // A number arriving on a path where it would *become* the stored value, with
  // a non-finite one substituted by 0 — the substitution ./ , .sqrt, .zmap,
  // .clip, .slide and .mean all make. Kept separate from the operand helpers
  // below because the identity that neutralises a non-finite operand depends on
  // the operation, and only this path has no operation to take one from.
  inline double AsValue(float value) {
    return std::isfinite(value) ? static_cast<double>(value) : 0.0;
  }

  // The same for an operand, neutralised with the identity of its operation
  // rather than with 0. For an add the two coincide; for a multiply they must
  // not, since 0 would zero the register and no later multiply could recover
  // it. See "Non-finite operands" in the header.
  inline double AsOperand(float value, double identity) {
    return std::isfinite(value) ? static_cast<double>(value) : identity;
  }

} // namespace

CONSTRUCT() {
  // Inlet 0 is hot: Max's left inlet replaces the stored value and emits.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  // Inlet 1 adds, inlet 2 multiplies; both silent, as Max documents.
  ADD_IN_1;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_2;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(initial);
  // The register has to be loaded from the argument *after* it is parsed, or a
  // saved `.accum 5` would answer 0 to its first bang. That is what the parse
  // callback is for; it also means a live edit of the argument reloads the
  // register, which is the honest reading of "the initial value changed" for an
  // object whose only parameter is where it starts.
  REG_PARM_PARSE;

  initial = 0.f;
  current = 0.0;

  ADD_DESCRIPTION(
      "A stored number to add into and multiply in place — the general-purpose register that "
      ".counter only covers the increment-by-one case of, for running totals, scaling chains and "
      "stateful math without a feedback cord. Three inlets, as in Max: an int or float on inlet 0 "
      "replaces the stored value and sends it out, a number on inlet 1 is added to it and a number "
      "on inlet 2 multiplies it, both without triggering output. A bang sends the stored value "
      "out, and before any operation that is the 'initial' creation argument (0 by default) rather "
      "than silence. The same three operations are available as messages on inlet 0 for patches "
      "that would rather send one cord than wire three: 'set <n>' stores without emitting (Max's "
      "word), 'add <n>' adds without emitting (Max spells it 'ft1', accepted as a synonym) and "
      "'mult <n>' multiplies without emitting. 'reset' restores the initial argument, also "
      "silently — as .counter's reset returns to startValue — which is not the same message as "
      "'set 0' whenever the object was created with an argument. The register is a double while "
      "the outlet carries a float, because a multiply-accumulate feeds its own rounding back into "
      "itself: over a chain of k operations the relative error grows as about k*2^-53, still 29 "
      "bits below what the outlet can express after a billion of them. Max's int/float duality — "
      "where an integer argument makes every multiply round back to an integer — is deliberately "
      "not reproduced, since '.accum 5' and '.accum 5.0' parse identically here and guessing wrong "
      "would silently turn two 'mult 0.5' into 0 instead of a quarter; put a .round on the outlet "
      "instead. At the limits the register saturates at plus or minus 3.4028235e38 rather than "
      "becoming infinite: an infinity is absorbing, so one overflow would make the register "
      "permanently useless and an infinity minus an infinity would put a NaN on the outlet that "
      "poisons everything downstream, while a saturated register is still a number with the right "
      "sign that the next multiply brings back into range. That matters because a "
      "multiply-accumulate diverges fast — repeated 'mult 2' reaches the ceiling in 128 messages. "
      "Clamping after every operation is also what makes the double arithmetic itself unable to "
      "overflow, both operands then being bounded by the float maximum. The bottom of the range is "
      "not floored, so a chain that multiplies down by 1e-30 and back up by 1e30 recovers exactly "
      "even though the outlet reported 0 in between. A non-finite number is neutralised with the "
      "identity of the operation it drives — 0 for an add, 1 for a multiply — and with 0 where it "
      "would become the stored value.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value",
            "Int or float to store and send out / bang to send the stored value out / list 'set "
            "<n>' to store without emitting / list 'add <n>' (or 'ft1 <n>') to add without "
            "emitting / list 'mult <n>' to multiply without emitting / list 'reset' to return to "
            "the initial argument.",
            "any float");
  INLET_DOC(1, "add", "Number added to the stored value, without triggering output.", "any float");
  INLET_DOC(2, "mult", "The stored value is multiplied by this, without triggering output.",
            "any float");
  OUTLET_DOC(0, "out", "The stored value.",
             "any float, saturating at +/-3.4028235e38 rather than becoming infinite");
  PARAM_DOC("initial", "0",
            "Starting value of the register, what a bang reports before any operation, and the "
            "value 'reset' returns to.",
            "any float");
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameters have been read.
  // Loading the register here rather than in the constructor is what lets a
  // saved `.accum 5` answer 5 to its first bang.
  current = Clamp(AsValue(initial));
}

double gAccum::Clamp(double value) {
  // A NaN compares false against everything, so it would fall through both
  // bounds unchanged; it is caught first and reported as the 0 it would have
  // been substituted with on the way in. An infinity needs no special case —
  // it fails the first comparison and saturates like any other value too
  // large, which is the answer that keeps the sign. Between them the register
  // can never hold a non-finite value by any route, which is what every other
  // method here relies on.
  if (std::isnan(value)) return 0.0;
  if (value > ACCUM_LIMIT) return ACCUM_LIMIT;
  if (value < -ACCUM_LIMIT) return -ACCUM_LIMIT;
  return value;
}

void gAccum::Replace(float value) {
  current = Clamp(AsValue(value));
}

void gAccum::Add(float value) {
  // Both terms are bounded by ACCUM_LIMIT, so the sum is at most 6.8e38 and
  // cannot overflow the double before Clamp() saturates it.
  current = Clamp(current + AsOperand(value, 0.0));
}

void gAccum::Multiply(float value) {
  // Likewise: the product is at most ACCUM_LIMIT^2 = 1.16e77, far below the
  // double's 1.8e308, so the saturation below is computed rather than caught
  // after an infinity has already appeared.
  current = Clamp(current * AsOperand(value, 1.0));
}

void gAccum::Emit(YSE::THREAD thread) {
  // `current` is finite and within +/-FLT_MAX by construction, so this cast is
  // exact at the ends of the range as well as in the middle.
  outputs[0].SendFloat(static_cast<float>(current), thread);
}

bool gAccum::MatchArg(const std::string& value, const char* word, std::size_t wordLength,
                      float& out) {
  // The word plus at least a separator and a digit. Comparing the length first
  // keeps compare() from being asked about a range it does not have.
  if (value.size() <= wordLength) return false;
  if (value.compare(0, wordLength, word) != 0) return false;

  // The word has to *end* where it ends, or `address 5` would read as `add 5`:
  // the shared reader skips tokens it cannot parse, so the leftover `ress`
  // would be stepped over and the 5 accepted.
  const char separator = value[wordLength];
  if (separator != ' ' && separator != '\t') return false;

  // ExprParseFloatList skips leading whitespace and neither allocates nor
  // touches locale state, so this is safe on whichever thread the message
  // arrived on. It returns 0 when there is no number, which is what makes
  // `mult` on its own an ignored message rather than a multiply by nothing.
  float parsed = 0.f;
  if (ExprParseFloatList(value.c_str() + wordLength, &parsed, 1) != 1) return false;
  out = parsed;
  return true;
}

FLOAT_IN(SetFloat) {
  switch (inlet) {
  case 0:
    // Max: "Replaces the value stored in accum, and sends the new value out the
    // outlet." The only path that emits without being asked to.
    Replace(value);
    Emit(thread);
    break;
  case 1:
    // Max: "The number is added to the stored value, without triggering output."
    Add(value);
    break;
  case 2:
    // Max: "The stored value is multiplied by the input, without triggering
    // output."
    Multiply(value);
    break;
  default:
    break;
  }
}

INT_IN(SetInt) {
  // The register is a float object throughout — see "Numeric type" in the
  // header for why Max's int variant is not reproduced.
  SetFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(Bang) {
  if (inlet != 0) return;
  // Max: "Outputs the value currently stored in accum." Before any operation
  // that is the creation argument, not silence.
  Emit(thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  float argument = 0.f;

  // Max: "The word set, followed by a number, sets the stored value to that
  // number, without triggering output."
  if (MatchArg(value, "set", 3, argument)) {
    Replace(argument);
    return;
  }

  // The message forms of inlets 1 and 2, so a patch can drive all three
  // operations down one cord. `ft1` is Max's spelling of the first of them.
  if (MatchArg(value, "add", 3, argument) || MatchArg(value, "ft1", 3, argument)) {
    Add(argument);
    return;
  }

  if (MatchArg(value, "mult", 4, argument)) {
    Multiply(argument);
    return;
  }

  if (value == "reset") {
    // Back to the creation argument, as .counter's reset returns to
    // startValue. Silent, like every other message here.
    current = Clamp(AsValue(initial));
    return;
  }

  // Anything else is not a message this object knows. Ignored rather than
  // guessed at, as in .slide and .mean.
}

GUI_VALUE() {
  return std::to_string(current);
}
