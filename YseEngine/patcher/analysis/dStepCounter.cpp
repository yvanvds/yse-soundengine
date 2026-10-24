#include "dStepCounter.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
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

  bool Is(const std::string& message, std::size_t begin, std::size_t length, const char* name,
          std::size_t nameLength) {
    return length == nameLength && message.compare(begin, nameLength, name) == 0;
  }

  // The number after a one-argument message; false when it is missing or not
  // a number.
  bool Argument(const std::string& message, std::size_t& cursor, float& number) {
    std::size_t begin = 0;
    std::size_t length = 0;
    if (!NextToken(message, cursor, begin, length)) return false;
    return ReadNumericToken(message.c_str() + begin, length, number);
  }

  // A mode from a number: 0 .. modes - 1, truncated; -1 when out of range.
  int ToMode(float number, int modes) {
    if (!(number >= 0.f && number < static_cast<float>(modes))) return -1;
    return static_cast<int>(number);
  }

  // The attribute keywords of the arguments and messages.
  enum class keyword { none, direction, startmode, syncupdate, thresh };

  keyword ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "direction") return keyword::direction;
    if (word == "startmode") return keyword::startmode;
    if (word == "syncupdate") return keyword::syncupdate;
    if (word == "thresh") return keyword::thresh;
    return keyword::none;
  }

  constexpr unsigned int SLOTS = YSE::DSP::stepCounter::MAX_STEPS;
} // namespace

#define className dStepCounter

CONSTRUCT_DSP() {
  // in 0: the signal to count the jumps of; float / int / list set the
  // sequence; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(SetList);

  // out 0: the impulses; out 1: the count; out 2: the step
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Counts the jumps in a signal, such as the resets of a ~phasor, through a "
                  "sequence of counts and sends an impulse each time a count is reached: a "
                  "sequence of durations in beats from a steady clock.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in",
            "The signal to count the jumps of, usually a ~phasor (unconnected reads 0). A jump is "
            "a change of at least the threshold from one sample to the next. A number replaces "
            "the sequence with that one count; a list replaces it with its numbers. "
            "'seq <counts...>' does the same (none empties it); 'reset' starts over; "
            "'direction <0|1|2>' (both, up, down); 'startmode <0|1>' (1: the first jump fires "
            "and starts the sequence); 'syncupdate <0|1|2>' (a new sequence at once, after the "
            "step, after the sequence); 'thresh <x>' (the smallest jump).",
            "");
  OUTLET_DOC(0, "out", "1 for one sample each time a step's count is reached, 0 otherwise.", "0-1");
  OUTLET_DOC(1, "count", "The jumps counted in the step playing, from 0.", "0 or more");
  OUTLET_DOC(2, "step", "The index of the step playing, its place in the sequence.", "0 or more");
  PARAM_DOC("seq", "",
            "The counts of the sequence, up to 64, each rounded and at least 1; empty, every "
            "jump is a step. 'direction <n>', 'startmode <n>', 'syncupdate <n>' and 'thresh <x>' "
            "among them set those attributes (default 0, 1, 0 and 0.75). Messages do not change "
            "what is saved.",
            "list of whole numbers, 1 or more");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and may write the DSP class directly. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  direction = DSP::stepCounter::BOTH;
  startmode = 1;
  syncupdate = DSP::stepCounter::IMMEDIATE;
  thresh = 0.75f;
  fn.syncUpdate(DSP::stepCounter::IMMEDIATE).sequence(nullptr, 0);
}

PARM_PARSE() {
  float values[SLOTS];
  unsigned int n = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    const keyword word = ToKeyword(token.c_str(), token.size());
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
        ++i;
        SetAttribute(static_cast<int>(word), number);
      }
      continue;
    }
    if (n < SLOTS && ReadNumericToken(token, number)) values[n++] = number;
  }
  fn.syncUpdate(DSP::stepCounter::IMMEDIATE).sequence(values, n);
}

void dStepCounter::SetAttribute(int which, float number) {
  int mode = -1;
  switch (static_cast<keyword>(which)) {
  case keyword::direction:
    mode = ToMode(number, 3);
    if (mode >= 0) direction = mode;
    break;
  case keyword::startmode:
    startmode = number != 0.f ? 1 : 0;
    break;
  case keyword::syncupdate:
    mode = ToMode(number, 3);
    if (mode >= 0) syncupdate = mode;
    break;
  case keyword::thresh:
    if (std::isfinite(number)) thresh = number;
    break;
  case keyword::none:
    break;
  }
}

void dStepCounter::PostValue(float value) {
  lists.Begin();
  lists.values[0].store(value, std::memory_order_relaxed);
  lists.End(1);
}

void dStepCounter::PostList(const std::string& text, std::size_t cursor) {
  lists.Begin();
  unsigned int n = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  while (n < SLOTS && NextToken(text, cursor, begin, length)) {
    if (!ReadNumericToken(text.c_str() + begin, length, number)) continue;
    lists.values[n++].store(number, std::memory_order_relaxed);
  }
  lists.End(n);
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
  PostList(value, 0);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  if (Is(message, begin, length, "seq", 3)) {
    PostList(message, cursor);
    return;
  }
  if (Is(message, begin, length, "reset", 5)) {
    resetPending.store(true, std::memory_order_release);
    return;
  }

  const keyword word = ToKeyword(message.c_str() + begin, length);
  float number = 0.f;
  if (word == keyword::none || !Argument(message, cursor, number)) return;
  SetAttribute(static_cast<int>(word), number);
}

CALC() {
  fn.direction(static_cast<DSP::stepCounter::DIRECTION>(direction.load()))
      .startMode(startmode != 0)
      .syncUpdate(static_cast<DSP::stepCounter::SYNC>(syncupdate.load()))
      .threshold(thresh.load());

  float table[SLOTS];
  unsigned int n = 0;
  if (lists.Take(table, n)) fn.sequence(table, n);
  if (resetPending.exchange(false, std::memory_order_acquire)) fn.reset();

  // An unconnected inlet reads 0 (#1036): the input never jumps.
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&fn.counts(), thread);
  outputs[2].SendBuffer(&fn.steps(), thread);
}

#undef className
