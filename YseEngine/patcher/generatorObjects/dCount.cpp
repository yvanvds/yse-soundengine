#include "dCount.h"
#include <cmath>
#include <limits>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  // The next token of `text` from `pos`, walked in place so nothing
  // allocates on whichever thread delivers the message. False when only
  // separators remain.
  bool NextToken(const char* text, std::size_t length, std::size_t& pos, std::size_t& begin,
                 std::size_t& size) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    begin = pos;
    while (pos < length && !IsSelectorSeparator(text[pos]))
      pos++;
    size = pos - begin;
    return size > 0;
  }

  // A number as a count, truncated like an int; false when it is not finite.
  bool ToCount(float value, int& count) {
    if (!std::isfinite(value)) return false;
    constexpr float top = 2147483520.f; // the largest float below 2^31
    if (value >= top) {
      count = std::numeric_limits<int>::max();
    } else if (value <= -top) {
      count = std::numeric_limits<int>::min();
    } else {
      count = static_cast<int>(value);
    }
    return true;
  }
} // namespace

#define className dCount

CONSTRUCT_DSP() {
  initial = 0;
  limit = 0;
  enable = 0;

  // in 0: enable signal, bang, int, float, min / set / stop, a list
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Restart);
  REG_INT_IN(StartAt);
  REG_FLOAT_IN(SetMinimum);

  // in 1: the limit (int or float)
  ADD_IN_1;
  REG_INT_IN(SetLimitInt);
  REG_FLOAT_IN(SetLimit);

  // out 0: the count; out 1: the carry count
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(initial);
  ADD_PARAM(limit);
  ADD_PARAM(enable);

  ADD_DESCRIPTION("A signal counting samples: it goes up by 1 at every sample from its initial "
                  "value, and can loop below a limit. For indexing into buffers and "
                  "sample-accurate timers.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "enable",
            "A signal counts while it is not 0. bang restarts from the initial value; an int "
            "sets it and restarts; a float, 'min <n>' and 'set <n>' set it without a restart; "
            "'stop' stops; '<initial> <limit> <enable>' sets all three.",
            "any");
  INLET_DOC(1, "limit",
            "The count limit, never output: the count goes back to the initial value instead. 0 "
            "means no limit. Does not restart.",
            "integer");
  OUTLET_DOC(0, "count", "The count, or the initial value while stopped.", "integer");
  OUTLET_DOC(1, "carry", "How many times the count has gone back to the initial value.",
             "0 or more");
  PARAM_DOC("initial", "0", "The value counting starts from and goes back to.", "integer");
  PARAM_DOC("limit", "0", "The count limit, never output; 0 means no limit.", "integer");
  PARAM_DOC("enable", "0", "1 counts from the start, 0 waits for a bang.", "0 or 1");
}

BANG_IN(Restart) {
  command = START;
}

INT_IN(StartAt) {
  // The minimum first: CALC() reads the command before the minimum.
  initial = value;
  command = START;
}

FLOAT_IN(SetMinimum) {
  int count = 0;
  if (ToCount(value, count)) initial = count;
}

FLOAT_IN(SetLimit) {
  int count = 0;
  if (ToCount(value, count)) limit = count;
}

INT_IN(SetLimitInt) {
  limit = value;
}

MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t pos = 0;
  std::size_t begin = 0;
  std::size_t size = 0;
  if (!NextToken(text, length, pos, begin, size)) return;

  float number = 0.f;
  int count = 0;
  if (ReadNumericToken(text + begin, size, number)) {
    // <initial> [<limit> [<enable> [<autoreset>]]]; autoreset is ignored.
    if (!ToCount(number, count)) return;
    initial = count;
    if (!NextToken(text, length, pos, begin, size)) return;
    if (!ReadNumericToken(text + begin, size, number) || !ToCount(number, count)) return;
    limit = count;
    if (!NextToken(text, length, pos, begin, size)) return;
    if (!ReadNumericToken(text + begin, size, number) || std::isnan(number)) return;
    command = number != 0.f ? START : STOP;
    return;
  }

  const auto is = [&](const char* name, std::size_t nameLength) {
    return size == nameLength && message.compare(begin, nameLength, name) == 0;
  };
  if (is("stop", 4)) {
    command = STOP;
    return;
  }
  if (!is("min", 3) && !is("set", 3)) return; // `autoreset` and others: ignored
  if (!NextToken(text, length, pos, begin, size)) return;
  if (ReadNumericToken(text + begin, size, number) && ToCount(number, count)) initial = count;
}

CALC() {
  // The command before the minimum, so an int's new minimum is seen by the
  // start it posts.
  const int pending = command.exchange(NONE);
  counter.minimum(initial);
  counter.limit(limit);

  const bool on = enable.load() != 0;
  if (!started) {
    started = true;
    appliedEnable = on;
    if (on) counter.start();
  } else if (on != appliedEnable) {
    // A live re-parse changed the argument: as a rebuilt object would.
    appliedEnable = on;
    if (on) {
      counter.start();
    } else {
      counter.stop();
    }
  }
  if (pending == START) {
    counter.start();
  } else if (pending == STOP) {
    counter.stop();
  }

  // An unconnected enable inlet leaves starting and stopping to messages
  // (#1036: it has no signal to read).
  DSP::buffer& out = inputs[0].IsConstant() ? counter() : counter(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&counter.carry(), thread);
}

#undef className
