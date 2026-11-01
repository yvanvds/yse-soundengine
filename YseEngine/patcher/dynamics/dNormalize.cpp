#include "dNormalize.h"
#include "../pListArgs.h"
#include "../pSelector.h"

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

#define className dNormalize

CONSTRUCT_DSP() {
  amplitude = DSP::normalize::DEFAULT_AMPLITUDE;
  peakValue = 0.f;

  // in 0: the signal to normalize; a number sets the peak; 'reset [peak]'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the target amplitude, a number or a signal; 'reset [peak]'
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the normalized signal
  ADD_OUT_BUFFER;

  ADD_PARAM(amplitude);

  ADD_DESCRIPTION("Normalizes a signal: scales it by a target amplitude divided by the largest "
                  "input amplitude so far, for level matching.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  INLET_DOC(0, "in",
            "The signal to normalize (unconnected reads 0). A number sets the peak, the "
            "largest input amplitude so far. 'reset' sets the peak to 0.000001, 'reset <peak>' "
            "to that value.",
            "any float");
  INLET_DOC(1, "amplitude",
            "The amplitude the peak is scaled to, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument). Negative "
            "inverts the signal. Takes 'reset' as the left inlet does.",
            "any float");
  OUTLET_DOC(0, "out",
             "The normalized signal: the input times the amplitude divided by the peak, never "
             "larger than the amplitude.",
             "any float");
  PARAM_DOC("amplitude", "1",
            "The target amplitude ('~normalize 0.5'). A number in the right inlet changes it, "
            "but what is saved stays the argument.",
            "any float");
}

// Left inlet: the peak; right inlet: the amplitude. The DSP class ignores a
// value that is not a finite number.
FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0) {
    peakValue = value;
    peakRequested = true;
  } else {
    amplitude = value;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  // 'reset' sets the peak to its floor, 'reset <peak>' to that value; a
  // malformed number is ignored.
  std::size_t next = 0;
  if (Token(message, 0, next) != "reset") return;
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!token.empty() && !ReadNumericToken(token.data(), token.size(), number)) return;
  // The DSP class raises 0 to its floor.
  peakValue = number;
  peakRequested = true;
}

CALC() {
  if (peakRequested.exchange(false)) fn.peak(peakValue.load());

  // Unconnected inlets (#1036): the input reads 0, the amplitude its number.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(amplitude.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
