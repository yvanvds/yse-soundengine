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

  ADD_DESCRIPTION("Stores a number you can replace, add to and multiply. A number on the left "
                  "inlet replaces it and sends it; the middle inlet adds to it and the right inlet "
                  "multiplies it, without sending.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "value",
            "Number: replace the stored value and send it. Bang: send the stored value. 'set <n>', "
            "'add <n>', 'mult <n>': replace, add or multiply without sending. 'reset': go back "
            "to the starting value.",
            "any float");
  INLET_DOC(1, "add", "Number added to the stored value, without sending.", "any float");
  INLET_DOC(2, "mult", "Number the stored value is multiplied by, without sending.", "any float");
  OUTLET_DOC(0, "out", "The stored value.", "any float");
  PARAM_DOC("initial", "0",
            "Starting value: what a bang sends before anything changes it, and what 'reset' "
            "returns to.",
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
