#include "dStepFun.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The next token of `message` from `cursor` on, walked in place so nothing
  // allocates on whichever thread delivers the message. False at the end.
  bool NextToken(const std::string& message, std::size_t& cursor, std::size_t& begin,
                 std::size_t& length) {
    const char* text = message.c_str();
    const std::size_t size = message.size();
    while (cursor < size && IsSelectorSeparator(text[cursor]))
      cursor++;
    begin = cursor;
    while (cursor < size && !IsSelectorSeparator(text[cursor]))
      cursor++;
    length = cursor - begin;
    return length > 0;
  }

  // The number after a one-argument message; false when it is missing or not
  // a number.
  bool Argument(const std::string& message, std::size_t& cursor, float& number) {
    std::size_t begin = 0;
    std::size_t length = 0;
    if (!NextToken(message, cursor, begin, length)) return false;
    return ReadNumericToken(message.c_str() + begin, length, number);
  }

  // A float truncated to an int, kept in range first.
  int ToInt(float value) {
    if (value > 1.0e6f) return 1000000;
    if (value < -1.0e6f) return -1000000;
    return static_cast<int>(value);
  }

  // A mode from a number: 0 .. modes - 1, truncated; -1 when out of range.
  int ToMode(float number, int modes) {
    if (!(number >= 0.f && number < static_cast<float>(modes))) return -1;
    return static_cast<int>(number);
  }

  // The words of the arguments and messages.
  enum class keyword { none, pattern, curvepattern, curvemode, syncupdate, step };

  keyword ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "pattern") return keyword::pattern;
    if (word == "curvepattern") return keyword::curvepattern;
    if (word == "curvemode") return keyword::curvemode;
    if (word == "syncupdate") return keyword::syncupdate;
    if (word == "step") return keyword::step;
    return keyword::none;
  }

  constexpr unsigned int SLOTS = YSE::DSP::stepFunction::MAX_STEPS;
} // namespace

#define className dStepFun

CONSTRUCT_DSP() {
  // in 0: the ramp; bang resets; float / int / list set the pattern; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Reset);
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(SetList);

  // in 1: the step, a signal or a number
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetStep);
  REG_INT_IN(SetStepInt);

  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Plays a breakpoint function one segment per cycle of an input ramp such as a "
                  "~phasor: each cycle travels from one value of the pattern to the next, for "
                  "stepped modulation and pitch sequences.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The 0-1 ramp that times the segments (unconnected reads 0). Each forward wrap moves "
            "to the next step, each backward wrap to the one before. A bang goes back to step 0. "
            "A number makes the pattern that one value, and is the first value of a list that "
            "follows it. A list is a breakpoint function as .function sends it, whose values "
            "become the pattern. 'pattern <values...>' and 'curvepattern <curves...>' (none "
            "clears); 'step <n>'; 'curvemode <0|1|2>'; 'syncupdate <0|1|2>'.",
            "0-1");
  INLET_DOC(1, "step",
            "The step to play, a signal such as ~stepdiv's index, rounded down and wrapped into "
            "the pattern; while connected it replaces the counting of the ramp's cycles. A "
            "number sets the step when no signal is connected.",
            "0 or more");
  OUTLET_DOC(0, "out",
             "The function: step k travels from value k-1 to value k as the ramp goes from 0 to "
             "1, step 0 from the last value to the first.",
             "");
  PARAM_DOC("pattern", "",
            "The values of the pattern, up to 64. 'curvepattern' makes the numbers after it the "
            "curves of the steps (-1 to 1, 0 linear) and 'pattern' makes them values again; "
            "'curvemode <n>' (how a list is read: 0 value-time pairs, 1 value-time-curve "
            "triples, 2 auto, pairs as .function sends; default 2) and 'syncupdate <n>' (a new "
            "pattern lands 0 at once, 1 when the ramp wraps, 2 when the sequence ends; default "
            "0) set those attributes. Messages do not change what is saved.",
            "list of numbers");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and may write the DSP class directly. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  curvemode = 2;
  syncupdate = DSP::stepFunction::IMMEDIATE;
  haveInitial = false;
  fn.syncUpdate(DSP::stepFunction::IMMEDIATE).pattern(nullptr, 0).curvePattern(nullptr, 0);
}

PARM_PARSE() {
  float valueTable[SLOTS];
  float curveTable[SLOTS];
  unsigned int nValues = 0;
  unsigned int nCurves = 0;
  bool toCurves = false;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    const keyword word = ToKeyword(token.c_str(), token.size());
    if (word == keyword::pattern || word == keyword::curvepattern) {
      toCurves = word == keyword::curvepattern;
      continue;
    }
    // A mode keyword takes the number after it; one with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
        ++i;
        const int mode = ToMode(number, 3);
        if (mode >= 0 && word == keyword::curvemode) curvemode = mode;
        if (mode >= 0 && word == keyword::syncupdate) syncupdate = mode;
      }
      continue;
    }
    if (!ReadNumericToken(token, number)) continue;
    if (toCurves) {
      if (nCurves < SLOTS) curveTable[nCurves++] = number;
    } else if (nValues < SLOTS) {
      valueTable[nValues++] = number;
    }
  }
  fn.syncUpdate(DSP::stepFunction::IMMEDIATE)
      .pattern(valueTable, nValues)
      .curvePattern(curveTable, nCurves);
}

void dStepFun::PostValue(float value) {
  values.Begin();
  values.values[0].store(value, std::memory_order_relaxed);
  values.End(1);
  initial = value;
  haveInitial = true;
}

void dStepFun::PostNumbers(dSubdiv::mailbox<float>& box, const std::string& text,
                           std::size_t cursor) {
  haveInitial = false;
  box.Begin();
  unsigned int n = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  while (n < SLOTS && NextToken(text, cursor, begin, length)) {
    if (!ReadNumericToken(text.c_str() + begin, length, number)) continue;
    box.values[n++].store(number, std::memory_order_relaxed);
  }
  box.End(n);
}

// A breakpoint function in line format: the first number of each pair (or
// triple) is a value, the third of a triple a curve; the times are the
// ramp's. Grouping numbers is parsing, not sample math.
void dStepFun::PostBreakpoints(const std::string& text) {
  const bool triples = curvemode == 1;
  const unsigned int stride = triples ? 3u : 2u;
  if (triples) curves.Begin();
  values.Begin();
  unsigned int n = 0;
  if (haveInitial) {
    values.values[n].store(initial, std::memory_order_relaxed);
    if (triples) curves.values[n].store(0.f, std::memory_order_relaxed);
    ++n;
  }
  haveInitial = false;
  unsigned int numbers = 0;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  float value = 0.f;
  while (n < SLOTS && NextToken(text, cursor, begin, length)) {
    if (!ReadNumericToken(text.c_str() + begin, length, number)) continue;
    // A group counts once it is complete, so a value waits for its time
    // (and curve).
    const unsigned int at = numbers++ % stride;
    if (at == 0) value = number;
    if (at != stride - 1) continue;
    values.values[n].store(value, std::memory_order_relaxed);
    if (triples) curves.values[n].store(number, std::memory_order_relaxed);
    ++n;
  }
  // The curves close first and the values last: a block that takes the
  // values finds the curves complete too.
  if (triples) curves.End(n);
  values.End(n);
}

void dStepFun::PostStep(int n) {
  stepTarget.store(n, std::memory_order_relaxed);
  stepPending.store(true, std::memory_order_release);
}

BANG_IN(Reset) {
  (void)inlet;
  (void)thread;
  haveInitial = false;
  resetPending.store(true, std::memory_order_release);
}

FLOAT_IN(SetValue) {
  (void)inlet;
  (void)thread;
  PostValue(value);
}

INT_IN(SetValueInt) {
  (void)inlet;
  (void)thread;
  PostValue(static_cast<float>(value));
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a command that came as a list, as text through a
  // receive does: the inlet hands every list to a list handler.
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  if (!NextToken(value, cursor, begin, length)) return;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) {
    SetMessage(value, 0.f);
    return;
  }
  PostBreakpoints(value);
}

FLOAT_IN(SetStep) {
  (void)inlet;
  (void)thread;
  PostStep(ToInt(value));
}

INT_IN(SetStepInt) {
  (void)inlet;
  (void)thread;
  PostStep(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  if (length == 4 && message.compare(begin, 4, "bang") == 0) {
    haveInitial = false;
    resetPending.store(true, std::memory_order_release);
    return;
  }

  float number = 0.f;
  switch (ToKeyword(message.c_str() + begin, length)) {
  case keyword::pattern:
    PostNumbers(values, message, cursor);
    break;
  case keyword::curvepattern:
    PostNumbers(curves, message, cursor);
    break;
  case keyword::step:
    haveInitial = false;
    if (Argument(message, cursor, number)) PostStep(ToInt(number));
    break;
  case keyword::curvemode:
    haveInitial = false;
    if (Argument(message, cursor, number) && ToMode(number, 3) >= 0) curvemode = ToMode(number, 3);
    break;
  case keyword::syncupdate:
    haveInitial = false;
    if (Argument(message, cursor, number) && ToMode(number, 3) >= 0) syncupdate = ToMode(number, 3);
    break;
  case keyword::none:
    break;
  }
}

CALC() {
  fn.syncUpdate(static_cast<DSP::stepFunction::SYNC>(syncupdate.load()));

  float table[SLOTS];
  unsigned int n = 0;
  // The values first: a list that set both closed its curves before them.
  if (values.Take(table, n)) fn.pattern(table, n);
  if (curves.Take(table, n)) fn.curvePattern(table, n);
  if (resetPending.exchange(false, std::memory_order_acquire)) fn.reset();
  if (stepPending.exchange(false, std::memory_order_acquire))
    fn.step(stepTarget.load(std::memory_order_relaxed));

  // An unconnected ramp reads 0 (#1036). A connected step signal gives the
  // step; without one the ramp's cycles count it.
  DSP::buffer& phase = inputs[0].Signal(0.f);
  DSP::buffer& out = inputs[1].IsConstant() ? fn(phase) : fn(phase, inputs[1].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
