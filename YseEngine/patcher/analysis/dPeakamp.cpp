#include "dPeakamp.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

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
} // namespace

#define className dPeakamp

CONSTRUCT_DSP() {
  interval = 0.f;
  signedPeak = 0;

  // in 0: the signal; a bang reports, a number sets the interval;
  // 'interval <ms>', 'signed <0|1>'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);
  REG_FLOAT_IN(SetInterval);
  REG_INT_IN(SetIntervalInt);

  // out 0: the peak, sent after the block (issue #1037)
  ADD_OUT_FLOAT;

  ADD_PARAM(interval);
  ADD_PARAM(signedPeak);

  ADD_DESCRIPTION("Reports the peak amplitude of a signal, the largest absolute sample value "
                  "since the last report: on a bang, or every interval.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to measure. A bang reports the peak so far and starts again; a number "
            "sets the reporting interval in milliseconds, 0 stops it. Also takes 'interval "
            "<ms>' and 'signed <0|1>'.",
            "any float");
  OUTLET_DOC(0, "peak",
             "The peak since the last report: the largest absolute sample value, or in signed "
             "mode that sample with its sign. 0 when nothing was measured.",
             "any float");
  PARAM_DOC("interval", "0",
            "Reporting interval in milliseconds ('~peakamp 100'). 0 reports only on a bang. A "
            "number or 'interval' changes it, but what is saved stays the argument.",
            "0 or more ms");
  PARAM_DOC("signed", "0",
            "1 reports the peak sample with its sign, 0 its absolute value ('~peakamp 100 1').",
            "0 or 1");
}

BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  report = true;
}

// The DSP class makes a negative or NaN interval 0.
FLOAT_IN(SetInterval) {
  (void)inlet;
  (void)thread;
  interval = value;
}

INT_IN(SetIntervalInt) {
  (void)inlet;
  (void)thread;
  interval = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // 'interval <ms>' and 'signed <0|1>'; a missing or malformed number is
  // ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  const bool isInterval = word == "interval";
  if (!isInterval && word != "signed") return;
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!ReadNumericToken(token.data(), token.size(), number) || std::isnan(number)) return;
  if (isInterval)
    interval = number;
  else
    signedPeak = number != 0.f ? 1 : 0;
}

CALC() {
  (void)thread;
  fn.interval(interval.load());
  fn.signedPeak(signedPeak.load() != 0);
  // Never Send* from CALC(): the patcher sends these after the traversal.
  // A bang reports the peak up to the end of the last block.
  if (report.exchange(false)) outputs[0].ArmFloat(fn.take());
  // Unconnected, the inlet reads as silence (#1036).
  if (fn(inputs[0].Signal(0.f)) > 0) outputs[0].ArmFloat(fn.lastReport());
}

#undef className
