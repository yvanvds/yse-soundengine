#include "dZerox.h"
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

#define className dZerox

CONSTRUCT_DSP() {
  volume = 1.f;

  // in 0: the input (signal, float or int); 'set <volume>'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetIn);
  REG_INT_IN(SetInInt);

  // out 0: the crossing count of the block, as a signal
  ADD_OUT_BUFFER;
  // out 1: a click on each crossing
  ADD_OUT_BUFFER;

  ADD_PARAM(volume);

  ADD_DESCRIPTION("Detects zero crossings: outputs the number of times the signal changed sign "
                  "in each block, a rough measure of noisiness or pitch, and a click on every "
                  "crossing.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. 'set "
            "<volume>' sets the height of the clicks.",
            "any float");
  OUTLET_DOC(0, "count",
             "The number of zero crossings in the block, the same on every sample of it. "
             "Samples that are exactly 0 are on neither side, so touching 0 is not a crossing.",
             "0 or more");
  OUTLET_DOC(1, "click",
             "The click volume on each sample where the signal has crossed zero, 0 elsewhere.",
             "0 or the click volume");
  PARAM_DOC("volume", "1",
            "The click volume, the height of each click ('~zerox 0.5'). 'set' changes it, but "
            "what is saved stays the argument.",
            "any float");
}

FLOAT_IN(SetIn) {
  (void)inlet;
  (void)thread;
  in = value;
}

INT_IN(SetInInt) {
  (void)inlet;
  (void)thread;
  in = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // 'set <volume>': the click volume. The DSP class makes NaN 0; a missing
  // or malformed number is ignored.
  std::size_t next = 0;
  if (Token(message, 0, next) != "set") return;
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (ReadNumericToken(token.data(), token.size(), number) && !std::isnan(number)) volume = number;
}

CALC() {
  fn.volume(volume.load());
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  fn(inputs[0].Signal(in.load()));
  // Right to left, as Max sends.
  outputs[1].SendBuffer(&fn.clicks(), thread);
  outputs[0].SendBuffer(&fn.counts(), thread);
}

#undef className
