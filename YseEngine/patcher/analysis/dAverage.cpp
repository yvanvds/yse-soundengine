#include "dAverage.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::movingAverage;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  // The mode a word names; false for any other word.
  bool ReadMode(std::string_view word, int& out) {
    if (word == "bipolar") {
      out = movingAverage::BIPOLAR;
    } else if (word == "absolute") {
      out = movingAverage::ABSOLUTE;
    } else if (word == "rms") {
      out = movingAverage::RMS;
    } else {
      return false;
    }
    return true;
  }

  // A number of samples as an int the DSP class clamps; NaN is rejected.
  bool ReadSamples(float value, int& out) {
    if (std::isnan(value)) return false;
    const auto most = static_cast<float>(movingAverage::MAXIMUM_CAPACITY);
    if (value >= most) {
      out = static_cast<int>(movingAverage::MAXIMUM_CAPACITY);
    } else if (value < 1.f) {
      out = 1;
    } else {
      out = static_cast<int>(std::lround(value));
    }
    return true;
  }
} // namespace

#define className dAverage

CONSTRUCT_DSP() {
  // in 0: the signal; a number sets the interval; mode words
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInterval);
  REG_INT_IN(SetIntervalInt);

  // out 0: the average
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("The running average of a signal over its last N samples, as a signal: the "
                  "plain mean (bipolar), the mean magnitude (absolute) or the root mean square "
                  "(rms). For envelope following and level detection.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to average; unconnected it reads as silence. A number sets the "
            "averaging interval in samples, clamped to 1 and the maximum. 'bipolar', "
            "'absolute' and 'rms' set the mode, as does 'mode' followed by one of them.",
            "any float");
  OUTLET_DOC(0, "average",
             "The average of the last interval samples at every sample: their mean in bipolar "
             "mode, their mean absolute value in absolute mode, their root mean square in rms "
             "mode. Before a full interval has arrived the missing samples count as 0.",
             "any float");
  PARAM_DOC("average", "100 bipolar",
            "The maximum averaging interval in samples, which is also the initial interval, "
            "then the mode: bipolar, absolute or rms ('~average 1000 rms'). The maximum sizes "
            "the history and is clamped to 1-1048576. Later numbers and messages do not change "
            "what is saved.",
            "a maximum interval, then a mode");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the history may be allocated here. Clear
// runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  if (fn.capacity() != movingAverage::DEFAULT_CAPACITY) fn = movingAverage();
  interval = static_cast<int>(fn.capacity());
  mode = movingAverage::BIPOLAR;
}

PARM_PARSE() {
  // The first number is the maximum interval, the first mode word the mode.
  bool haveMaximum = false;
  bool haveMode = false;
  for (const std::string& token : arguments) {
    int parsed = 0;
    if (!haveMode && ReadMode(token, parsed)) {
      mode = parsed;
      haveMode = true;
      continue;
    }
    float number = 0.f;
    if (!haveMaximum && ReadNumericToken(token.data(), token.size(), number) &&
        ReadSamples(number, parsed)) {
      if (static_cast<UInt>(parsed) != fn.capacity()) fn = movingAverage(static_cast<UInt>(parsed));
      interval = static_cast<int>(fn.capacity());
      haveMaximum = true;
    }
  }
}

// The DSP class clamps the interval to its capacity.
FLOAT_IN(SetInterval) {
  (void)inlet;
  (void)thread;
  int samples = 0;
  if (ReadSamples(value, samples)) interval = samples;
}

INT_IN(SetIntervalInt) {
  (void)inlet;
  (void)thread;
  interval = value < 1 ? 1 : value;
}

MESSAGES() {
  (void)value;
  // 'bipolar', 'absolute', 'rms', or 'mode <word>'; anything else is
  // ignored.
  std::size_t next = 0;
  std::string_view word = Token(message, 0, next);
  if (word == "mode") word = Token(message, next, next);
  int parsed = 0;
  if (ReadMode(word, parsed)) mode = parsed;
}

CALC() {
  fn.length(static_cast<UInt>(interval.load()));
  fn.mode(static_cast<movingAverage::MODE>(mode.load()));
  // Unconnected, the inlet reads as silence (#1036).
  outputs[0].SendBuffer(&fn(inputs[0].Signal(0.f)), thread);
}

#undef className
