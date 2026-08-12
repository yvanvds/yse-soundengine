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
      "The value, and Max's grammar for it. A bang emits the stored value without moving it — the "
      "arrows move it, so a bang is a read. An int or a float sets the value and emits nothing, "
      "Max being explicit that the number arriving here 'is not output directly'; that is what "
      "lets a '.loadmess' or a preset place the stepper without firing everything downstream. A "
      "float is truncated, as it is everywhere in Max. Three words are commands rather than "
      "numbers: 'inc' and 'dec' step up and down by the step size and emit the result, and "
      "'set <n>' is the int message spelled out. A list whose first token is a number is that "
      "number, so a stepper can be set from anything that emits a list. Whatever arrives, the "
      "value that ends up stored is inside the range.";

  constexpr char kNudgeInletDoc[] =
      "The two arrows on a cord. An int 'n' moves n steps — positive up, negative down — and emits "
      "the result, so a MIDI encoder that sends +1 and -1 drives the stepper with one connection "
      "and no message boxes. 0 moves nothing and still emits, which makes it a read exactly like a "
      "bang on the left inlet. A float is truncated first, and a list whose first token is a "
      "number is that number.";

  constexpr char kStepInletDoc[] =
      "Sets the step size live, as '.counter''s right inlet does — the coarse/fine pair a "
      "performance surface wants. A negative step swaps the arrows: 'inc' then goes down. A step "
      "of 0 leaves the value where it is, and the object still emits it. A float is truncated.";

  constexpr char kDescription[] =
      "Stepper: increments and decrements a stored whole number by a settable step, inside "
      "settable bounds. Max's 'incdec', the pair of arrows over a number, and the control for "
      "anything nudged rather than swept — transpose, octave, preset slot, bar count. It is "
      "deliberately not '.counter' with more parameters, because a bang means the opposite thing "
      "on the two objects: '.counter' steps on a bang, which is what makes it the thing behind a "
      "'.metro', while this one emits the stored value on a bang and moves only when an arrow is "
      "clicked. The left inlet is Max's: bang reads, an int or float sets without emitting, and "
      "the words 'inc', 'dec' and 'set <n>' step up, step down and set. The middle inlet is those "
      "arrows on a cord — an int moves that many steps, signed, and emits — and the right inlet "
      "sets the step size live. The minimum and maximum are taken as an ordered pair, so limits "
      "given the wrong way round still bound against the right two numbers, and 'wrap' chooses "
      "what happens at the end of the range: 0 clamps against it, 1 carries around to the other "
      "side. The wrapped range is inclusive at both ends — one past the maximum is the minimum — "
      "because this is a stepper over whole numbers and 11 stepping to 0 is what a twelve-note "
      "pitch class does. The default range is the whole int range, so an object with no arguments "
      "is unbounded and saturates rather than overflowing at the ends. The value is bounded on the "
      "way out as well as on the way in, so a re-range takes effect at once and a stepper created "
      "over 60-72 reads back as 60 rather than as the 0 it was born with; with wrap on, that "
      "untouched start lands wherever the wrap puts it, so place it with 'set' if the starting "
      "point matters. All the arithmetic runs in 64 bits and is bounded back into an int before it "
      "leaves, so no step, set or range can overflow. The GUI value is the stored value. Nothing "
      "on any message path allocates, locks or blocks on I/O, and Calculate() does nothing — this "
      "object sends from its handlers, which is what lets a set stay silent while a bang emits.";

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
  OUTLET_DOC(0, "out",
             "The stored value, sent whenever the stepper moves or is read. Always inside the "
             "minimum-maximum range.",
             "minimum-maximum");
  PARAM_DOC("step", "1", "Amount one step moves the value; a negative step swaps the arrows.",
            "any int");
  PARAM_DOC("minimum", "-2147483648", "Bottom of the range.", "any int");
  PARAM_DOC("maximum", "2147483647", "Top of the range.", "any int");
  PARAM_DOC("wrap", "0", "0 clamps at the ends of the range, 1 wraps around to the other end.",
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
      // `set` is the int message spelled out, so it stores and emits nothing.
      // A bare `set` names no value, so it does nothing rather than guessing at
      // one.
      std::size_t offset = end;
      int number = 0;
      if (ReadIntArgAt(value, offset, number)) Store(number);
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
