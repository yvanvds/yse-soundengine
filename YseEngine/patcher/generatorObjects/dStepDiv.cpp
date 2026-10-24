#include "dStepDiv.h"
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

  // A float truncated to an int, kept in range first.
  int ToInt(float value) {
    if (value > 1.0e6f) return 1000000;
    if (value < -1.0e6f) return -1000000;
    return static_cast<int>(value);
  }

  constexpr unsigned int SLOTS = YSE::DSP::phasorSubdiv::MAX_PATTERN;
} // namespace

#define className dStepDiv

CONSTRUCT_DSP() {
  // in 0: the ramp to divide (signal); bang; float / int / list set the
  // pattern; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Start);
  REG_FLOAT_IN(SetTime);
  REG_INT_IN(SetTimeInt);
  REG_LIST_IN(SetList);

  // in 1: jump to a step
  ADD_IN_1;
  REG_FLOAT_IN(SetStep);
  REG_INT_IN(SetStepInt);

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(curvemode);
  ADD_PARAM(loop);
  ADD_PARAM(syncupdate);

  ADD_DESCRIPTION("Plays a fresh 0-1 ramp for each step of a stepped function, with the step "
                  "number beside it: per-step envelopes in a sequence. The steps share each cycle "
                  "of an input ramp such as a ~phasor, or run free over times in milliseconds.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "A 0-1 ramp to divide; with none connected the steps run free over their times in "
            "ms. A bang starts the free-running steps. A number is a pattern of one step; a list "
            "is a breakpoint function as .function sends it, whose times become the steps. "
            "'pattern <times...>' (none clears); 'what <onsets...>' gives the moments the steps "
            "end instead; 'curvemode <0|1|2>'; 'loop <0|1>'; 'syncupdate <0|1>'.",
            "0-1");
  INLET_DOC(1, "step", "Jumps the free-running steps to the start of this step and plays on.",
            "0 or more");
  OUTLET_DOC(0, "out",
             "A 0-1 ramp per step. Free running and stopped: 0 before the first start, 1 after "
             "the last step.",
             "0-1");
  OUTLET_DOC(1, "index", "The number of the step playing, its place in the pattern.", "0 or more");
  PARAM_DOC("curvemode", "2",
            "How a list is read: 0 value-time pairs, 1 value-time-curve triples, 2 auto (pairs, "
            "as .function sends).",
            "0-2");
  PARAM_DOC("loop", "0", "1: free-running steps start again after the last one.", "0-1");
  PARAM_DOC("syncupdate", "0",
            "1: a new pattern waits for the input ramp to wrap, or for the free-running steps to "
            "end or restart.",
            "0-1");
}

void dStepDiv::PostTime(float time) {
  patterns.Begin();
  patterns.values[0].store(TIMES, std::memory_order_relaxed);
  patterns.values[1].store(time, std::memory_order_relaxed);
  patterns.End(2);
}

BANG_IN(Start) {
  (void)inlet;
  (void)thread;
  startPending = true;
}

FLOAT_IN(SetTime) {
  (void)inlet;
  (void)thread;
  PostTime(value);
}

INT_IN(SetTimeInt) {
  (void)inlet;
  (void)thread;
  PostTime(static_cast<float>(value));
}

// A breakpoint function in line format: the times of its pairs (or triples).
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a command that came as a list, as text through a
  // receive does: the inlet hands every list to a list handler.
  {
    std::size_t cursor = 0;
    std::size_t begin = 0;
    std::size_t length = 0;
    float number = 0.f;
    if (!NextToken(value, cursor, begin, length)) return;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) {
      SetMessage(value, 0.f);
      return;
    }
  }
  const unsigned int stride = curvemode == 1 ? 3u : 2u;
  patterns.Begin();
  patterns.values[0].store(TIMES, std::memory_order_relaxed);
  unsigned int n = 1;
  unsigned int numbers = 0;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  float time = 0.f;
  while (n < SLOTS && NextToken(value, cursor, begin, length)) {
    if (!ReadNumericToken(value.c_str() + begin, length, number)) continue;
    // The second number of a group is its time; it counts once the group is
    // complete, so a time waits for its curve.
    const unsigned int at = numbers++ % stride;
    if (at == 1) time = number;
    if (at == stride - 1) patterns.values[n++].store(time, std::memory_order_relaxed);
  }
  patterns.End(n);
}

FLOAT_IN(SetStep) {
  (void)inlet;
  (void)thread;
  stepTarget = ToInt(value);
  stepPending.store(true, std::memory_order_release);
}

INT_IN(SetStepInt) {
  (void)inlet;
  (void)thread;
  stepTarget = value;
  stepPending.store(true, std::memory_order_release);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  if (Is(message, begin, length, "bang", 4)) {
    startPending = true;
    return;
  }

  const bool isPattern = Is(message, begin, length, "pattern", 7);
  const bool isWhat = Is(message, begin, length, "what", 4);
  if (isPattern || isWhat) {
    patterns.Begin();
    patterns.values[0].store(isWhat ? ONSETS : TIMES, std::memory_order_relaxed);
    unsigned int n = 1;
    std::size_t arg = 0;
    std::size_t argLength = 0;
    float number = 0.f;
    while (n < SLOTS && NextToken(message, cursor, arg, argLength)) {
      if (!ReadNumericToken(message.c_str() + arg, argLength, number)) continue;
      patterns.values[n++].store(number, std::memory_order_relaxed);
    }
    patterns.End(n);
    return;
  }

  float number = 0.f;
  if (Is(message, begin, length, "curvemode", 9)) {
    if (Argument(message, cursor, number)) {
      const int mode = ToInt(number);
      if (mode >= 0 && mode <= 2) curvemode = mode;
    }
  } else if (Is(message, begin, length, "loop", 4)) {
    if (Argument(message, cursor, number)) loop = number != 0.f ? 1 : 0;
  } else if (Is(message, begin, length, "syncupdate", 10)) {
    if (Argument(message, cursor, number)) syncupdate = number != 0.f ? 1 : 0;
  }
}

CALC() {
  fn.loop(loop != 0).syncUpdate(syncupdate != 0);

  float table[SLOTS];
  unsigned int n = 0;
  if (patterns.Take(table, n) && n > 0) {
    if (table[0] == ONSETS)
      fn.onsets(table + 1, n - 1);
    else
      fn.pattern(table + 1, n - 1);
  }
  if (startPending.exchange(false)) fn.start();
  if (stepPending.exchange(false, std::memory_order_acquire)) fn.step(stepTarget);

  // Connected: the steps share the input's cycles. Unconnected: they run
  // free (#1036: an unconnected inlet has no signal to read).
  DSP::buffer& out = inputs[0].IsConstant() ? fn() : fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&fn.index(), thread);
}

#undef className
