// `.incdec` (issue #558). See gIncDec.h for the design — in particular for why
// this is a separate object from `.counter` rather than four more parameters on
// it. This file is the grammar and the bounding.
#include "gIncDec.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <climits>
#include <cstddef>

using namespace YSE::PATCHER;

#define className gIncDec

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.sustain`, `.flush` and `.makenote` read
  // their lists the same way.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // A token compared against a command word, without building a string to do it.
  bool TokenIs(const std::string& text, std::size_t begin, std::size_t length, const char* word,
               std::size_t wordLength) {
    return length == wordLength && text.compare(begin, length, word, wordLength) == 0;
  }

  constexpr char kControlInletDoc[] =
      "A bang sends the value without moving it. A number, or 'set <n>', sets it without sending. "
      "'inc' and 'dec' move one step up or down and send. Also takes its GUI value back, as a "
      "one-number list or 'set 0 <value>'.";

  constexpr char kNudgeInletDoc[] =
      "An int n moves n steps (negative moves down) and sends the result; 0 just sends the "
      "value. A float is truncated.";

  constexpr char kStepInletDoc[] =
      "Sets the step size. A negative step makes 'inc' go down. A float is truncated.";

  constexpr char kDescription[] =
      "A stepper over a whole number. 'inc' and 'dec', or a signed step count on the middle "
      "inlet, move the value inside a range that clamps or wraps; a number sets it without "
      "sending and a bang sends it.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_INT;

  // The defaults have to be in place before Register() hands the fields to the
  // parameter system: a creation argument overwrites them, no argument leaves
  // them alone. The whole int range is "unbounded", which makes clamping a
  // no-op and an argument-less object a two-directional `.counter`.
  current = 0;
  step = 1;
  minimum = INT_MIN;
  maximum = INT_MAX;
  wrap = 0;

  ADD_PARAM(step);
  ADD_PARAM(minimum);
  ADD_PARAM(maximum);
  ADD_PARAM(wrap);

  ADD_DESCRIPTION(kDescription);
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "value", kControlInletDoc, "any int, 'inc', 'dec', 'set <n>'");
  INLET_DOC(1, "nudge", kNudgeInletDoc, "any int");
  INLET_DOC(2, "step", kStepInletDoc, "any int");
  OUTLET_DOC(0, "out", "The value, whenever it moves or is read.", "minimum-maximum");
  PARAM_DOC("step", "1",
            "How far one step moves the value; a negative step reverses 'inc' and 'dec'.",
            "any int");
  PARAM_DOC("minimum", "-2147483648", "The bottom of the range.", "any int");
  PARAM_DOC("maximum", "2147483647", "The top of the range.", "any int");
  PARAM_DOC("wrap", "0", "0 stops at the ends of the range; 1 wraps around to the other end.",
            "0 or 1");
}

int gIncDec::Bound(I64 value) const {
  // The limits may arrive the wrong way round, so work against the ordered pair
  // rather than trusting minimum < maximum — `.pong`'s rule, for `.pong`'s
  // reason.
  I64 lo = minimum;
  I64 hi = maximum;
  if (lo > hi) {
    const I64 swap = lo;
    lo = hi;
    hi = swap;
  }

  if (wrap == 0) {
    if (value < lo) return (int)lo;
    if (value > hi) return (int)hi;
    return (int)value;
  }

  // Inclusive at both ends, so one past the top is the bottom — see the class
  // comment on why this is not `.pong`'s half-open float wrap. The span is at
  // least 1 (hi >= lo after the ordering above), so the % below cannot divide
  // by zero, and at most 2^32, which is why all of this is I64.
  const I64 span = (hi - lo) + 1;
  I64 offset = (value - lo) % span;
  // % keeps the sign of its left operand, so a value below the range comes back
  // negative and has to be lifted by one span.
  if (offset < 0) offset += span;
  return (int)(lo + offset);
}

int gIncDec::Value() const {
  // Bounded again on the way out: that is what makes a live re-range visible at
  // once, and what keeps an untouched stepper inside the range it was created
  // with. Idempotent on a value already inside the range, in both modes.
  return Bound(current.load());
}

void gIncDec::Store(I64 value) {
  current.store(Bound(value));
}

void gIncDec::Move(I64 steps, YSE::THREAD thread) {
  // I64 throughout: `steps` and `step` are both ints, so their product needs 62
  // bits in the worst case, and a step of INT_MIN has no representable negation
  // as an int at all.
  Store((I64)Value() + (steps * (I64)step));
  outputs[0].SendInt(Value(), thread);
}

void gIncDec::Number(int value, int inlet, YSE::THREAD thread) {
  switch (inlet) {
  case 0:
    // Max: the number arriving here "is not output directly".
    Store(value);
    break;
  case 1:
    Move(value, thread);
    break;
  default:
    step = value;
    break;
  }
}

BANG_IN(BangIn) {
  // A read, not a step — the difference from `.counter`, and the reason the two
  // objects are two objects.
  outputs[0].SendInt(Value(), thread);
}

INT_IN(IntIn) {
  Number(value, inlet, thread);
}

FLOAT_IN(FloatIn) {
  // Max truncates a float to an int in every inlet. ExprToInt rather than a
  // cast: a value outside the int range is undefined behaviour to cast.
  Number(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  if (inlet == 0) {
    // Max's command words, left inlet only and words rather than numbers, so
    // none of them can be set by accident.
    if (TokenIs(value, begin, length, "inc", 3)) {
      Move(1, thread);
      return;
    }
    if (TokenIs(value, begin, length, "dec", 3)) {
      Move(-1, thread);
      return;
    }
    if (TokenIs(value, begin, length, "set", 3)) {
      // Two grammars share the word, told apart by counting the numbers.
      // One number is Max's `set <n>` — the int message spelled out, so it
      // stores and emits nothing. Two numbers are issue #551's cell write
      // `set <index> <value>` (the #846 migration): range-checked against the
      // one cell there is, so any other index is dropped, never folded onto
      // cell 0. A bare `set` names no value, so it does nothing rather than
      // guessing at one.
      std::size_t offset = end;
      int first = 0;
      if (!ReadIntArgAt(value, offset, first)) return;
      int second = 0;
      if (ReadIntArgAt(value, offset, second)) {
        if (first == 0) Store(second);
        return;
      }
      Store(first);
      return;
    }
  }

  // Anything else is the number the first token spells, handled exactly as an
  // int on this inlet. Read as a decimal integer rather than through a float:
  // this object is integer-valued from end to end, and a float has 24 bits of
  // mantissa where an int has 31, so `set 2000000001` would not survive the
  // trip. Saturates at the int limits, and a fractional part is truncated as it
  // is everywhere in Max. A token that is not a number at all is ignored.
  std::size_t offset = begin;
  int number = 0;
  if (ReadIntArgAt(value, offset, number)) Number(number, inlet, thread);
}

GUI_VALUE() {
  return std::to_string(Value());
}

#undef className
