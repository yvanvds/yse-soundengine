#include "dWhat.h"
#include "../pListArgs.h"
#include "../pSelector.h"

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

  // A trigger mode from a number: 0-3, truncated; -1 when out of range.
  int ToMode(float number) {
    if (!(number >= 0.f && number < 4.f)) return -1;
    return static_cast<int>(number);
  }

  constexpr unsigned int SLOTS = YSE::DSP::phasorWhat::MAX_MATCHES;
} // namespace

#define className dWhat

CONSTRUCT_DSP() {
  // in 0: the ramp to watch (signal); float / int / list set the values;
  // messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(SetList);

  // out 0: the impulses
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Sends an impulse, a 1 for one sample, each time a 0-1 ramp such as a ~phasor "
                  "passes one of a list of phase values: rhythmic triggers on a phasor clock.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "phase",
            "The ramp to watch, a signal from 0 to 1, rising or falling (unconnected reads 0). "
            "A number replaces the values with that one value; a list replaces them with its "
            "numbers. 'matches <values...>' does the same (none empties the list); 'clear' "
            "empties it; 'triggermode <0|1|2|3>' (rising, falling, both, exactly equal); "
            "'syncupdate <0|1>' (1: a new list waits for the ramp to wrap).",
            "0-1");
  OUTLET_DOC(0, "out", "1 for one sample each time the ramp passes a value, 0 otherwise.", "0-1");
  PARAM_DOC("values", "",
            "The phase values to watch, up to 64, each wrapped into 0-1. 'triggermode <n>' and "
            "'syncupdate <n>' among them set those modes (default 2 and 0). Messages do not "
            "change what is saved.",
            "list of floats, 0 to 1");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and may write the DSP class directly. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  triggermode = DSP::phasorWhat::BOTH;
  syncupdate = 0;
  fn.syncUpdate(false).clear();
}

PARM_PARSE() {
  float values[SLOTS];
  unsigned int n = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    const bool keyword = token == "triggermode" || token == "syncupdate";
    // A keyword takes the number after it; a keyword with none is skipped.
    if (keyword) {
      if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
        ++i;
        if (token == "syncupdate") {
          syncupdate = number != 0.f ? 1 : 0;
        } else {
          const int mode = ToMode(number);
          if (mode >= 0) triggermode = mode;
        }
      }
      continue;
    }
    if (n < SLOTS && ReadNumericToken(token, number)) values[n++] = number;
  }
  fn.syncUpdate(false).matches(values, n);
}

void dWhat::PostValue(float value) {
  lists.Begin();
  lists.values[0].store(value, std::memory_order_relaxed);
  lists.End(1);
}

void dWhat::PostList(const std::string& text, std::size_t cursor) {
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

  if (Is(message, begin, length, "matches", 7)) {
    PostList(message, cursor);
    return;
  }
  if (Is(message, begin, length, "clear", 5)) {
    lists.Begin();
    lists.End(0);
    return;
  }

  float number = 0.f;
  if (Is(message, begin, length, "triggermode", 11)) {
    if (Argument(message, cursor, number)) {
      const int mode = ToMode(number);
      if (mode >= 0) triggermode = mode;
    }
  } else if (Is(message, begin, length, "syncupdate", 10)) {
    if (Argument(message, cursor, number)) syncupdate = number != 0.f ? 1 : 0;
  }
}

CALC() {
  fn.triggerMode(static_cast<DSP::phasorWhat::TRIGGER_MODE>(triggermode.load()))
      .syncUpdate(syncupdate != 0);

  float table[SLOTS];
  unsigned int n = 0;
  if (lists.Take(table, n)) fn.matches(table, n);

  // An unconnected inlet reads 0 (#1036): the input never moves.
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
