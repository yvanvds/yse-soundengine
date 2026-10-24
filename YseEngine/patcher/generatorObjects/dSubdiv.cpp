#include "dSubdiv.h"
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
} // namespace

#define className dSubdiv

CONSTRUCT_DSP() {
  // in 0: the ramp to subdivide (signal); float / int set div; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDiv);
  REG_INT_IN(SetDivInt);

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(div);
  ADD_PARAM(syncupdate);
  ADD_PARAM(silentmode);
  ADD_PARAM(lockprob);

  ADD_DESCRIPTION("Splits each cycle of a 0-1 ramp such as a ~phasor into a whole number of "
                  "shorter ramps, in phase with it: tuplets and ratchets. Duration and "
                  "probability patterns make uneven and skipped steps.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The ramp to subdivide, a signal from 0 to 1 (unconnected reads 0). A number sets the "
            "subdivisions. 'div <n>'; 'pattern <steps...>' sets step lengths in subdivisions "
            "(none clears); 'prob <chances...>' sets a chance 0-1 per step ('prob none' "
            "clears); 'lockprob <0|1>'; 'silentmode <0|1>'; 'syncupdate <0|1|2>'.",
            "0-1");
  OUTLET_DOC(0, "out", "The sub-ramps; 0 during a step that does not play.", "0-1");
  OUTLET_DOC(1, "index",
             "The number of the step playing. During a step that does not play, the last one "
             "that did (silentmode 0) or -1 (silentmode 1).",
             "-1 or more");
  PARAM_DOC("div", "1", "Subdivisions of each input cycle.", "1-16384");
  PARAM_DOC("syncupdate", "0",
            "When a new div or pattern lands: 0 at once, 1 when the next output step starts, 2 "
            "when the input next wraps.",
            "0-2");
  PARAM_DOC("silentmode", "0",
            "Index output of a step that does not play: 0 the last step that played, 1 -1.", "0-1");
  PARAM_DOC("lockprob", "0",
            "0: probabilities count subdivisions, so they keep their place on the grid; 1: they "
            "count steps of the duration pattern.",
            "0-1");
}

FLOAT_IN(SetDiv) {
  (void)inlet;
  (void)thread;
  div = ToInt(value);
}

INT_IN(SetDivInt) {
  (void)inlet;
  (void)thread;
  div = value;
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  if (Is(message, begin, length, "pattern", 7)) {
    steps.Begin();
    unsigned int n = 0;
    std::size_t arg = 0;
    std::size_t argLength = 0;
    float number = 0.f;
    while (n < DSP::phasorSubdiv::MAX_PATTERN && NextToken(message, cursor, arg, argLength)) {
      if (!ReadNumericToken(message.c_str() + arg, argLength, number)) continue;
      steps.values[n++].store(ToInt(number), std::memory_order_relaxed);
    }
    steps.End(n);
    return;
  }

  if (Is(message, begin, length, "prob", 4)) {
    chances.Begin();
    unsigned int n = 0;
    std::size_t arg = 0;
    std::size_t argLength = 0;
    float number = 0.f;
    while (n < DSP::phasorSubdiv::MAX_PATTERN && NextToken(message, cursor, arg, argLength)) {
      if (!ReadNumericToken(message.c_str() + arg, argLength, number)) continue; // "none"
      chances.values[n++].store(number, std::memory_order_relaxed);
    }
    chances.End(n);
    return;
  }

  float number = 0.f;
  if (Is(message, begin, length, "div", 3)) {
    if (Argument(message, cursor, number)) div = ToInt(number);
  } else if (Is(message, begin, length, "lockprob", 8)) {
    if (Argument(message, cursor, number)) lockprob = number != 0.f ? 1 : 0;
  } else if (Is(message, begin, length, "silentmode", 10)) {
    if (Argument(message, cursor, number)) silentmode = number != 0.f ? 1 : 0;
  } else if (Is(message, begin, length, "syncupdate", 10)) {
    if (Argument(message, cursor, number)) {
      const int mode = ToInt(number);
      if (mode >= 0 && mode <= 2) syncupdate = mode;
    }
  }
}

CALC() {
  const int landing = syncupdate;
  fn.syncUpdate(landing == 1   ? DSP::phasorSubdiv::update::output
                : landing == 2 ? DSP::phasorSubdiv::update::input
                               : DSP::phasorSubdiv::update::now)
      .silentMode(silentmode != 0 ? DSP::phasorSubdiv::silent::minusOne
                                  : DSP::phasorSubdiv::silent::hold)
      .lockProb(lockprob != 0)
      .div(div);

  int stepTable[DSP::phasorSubdiv::MAX_PATTERN];
  unsigned int n = 0;
  if (steps.Take(stepTable, n)) fn.pattern(stepTable, n);
  float chanceTable[DSP::phasorSubdiv::MAX_PATTERN];
  if (chances.Take(chanceTable, n)) fn.prob(chanceTable, n);

  // An unconnected inlet reads 0 (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&fn.index(), thread);
}

#undef className
