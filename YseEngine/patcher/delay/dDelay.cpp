#include "dDelay.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <cstring>
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

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }

  // A time value from `from` on: a number, optionally followed by `samples`
  // or `ms`, and nothing after it.
  bool ReadTime(const std::string& text, std::size_t from, float& value, bool& ms) {
    std::size_t next = 0;
    if (!ReadNumber(Token(text, from, next), value)) return false;
    const std::string_view unit = Token(text, next, next);
    std::size_t rest = 0;
    if (!Token(text, next, rest).empty()) return false;
    if (unit.empty() || unit == "samples") {
      ms = false;
      return true;
    }
    if (unit == "ms") {
      ms = true;
      return true;
    }
    return false;
  }
} // namespace

#define className dDelay

CONSTRUCT_DSP() {
  // in 0: the signal to delay; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the delay time in samples, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDelay);
  REG_INT_IN(SetDelayInt);

  // out 0: the delayed signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Delays a signal by a number of samples, from none at all up to the memory set "
                  "when it is made. A number delay crossfades over the ramp time; a signal "
                  "delay is read per sample and interpolated. For alignment and short delays.");
  ADD_CATEGORY(pCategory::DELAY);
  INLET_DOC(0, "in",
            "The signal to delay (unconnected reads 0). 'clear' silences the line, 'maxsize <n>' "
            "sets the longest delay in samples (up to the memory) and clears it, 'ramp <ms>' "
            "sets the crossfade time, and '<n>', '<n> samples', '<n> ms' or 'delay <time>' set "
            "the delay.",
            "");
  INLET_DOC(1, "delay",
            "The delay time. A number is samples, the fraction dropped, and crossfades over the "
            "ramp time; a signal is read per sample in samples, interpolated, and wins while it "
            "is connected. Clamped to 0 and the longest delay. Takes the same words as the left "
            "inlet.",
            "0 to the longest delay");
  OUTLET_DOC(0, "out", "The delayed signal.", "");
  PARAM_DOC("delay", "512 0 0",
            "The memory in samples, the initial delay in samples and the ramp time in ms, up to "
            "three numbers ('~delay 44100 200 10'; they default to 512, 0 and 0). The memory is "
            "allocated when the object is made and is clamped to 0-11520000. Later numbers and "
            "messages do not change what is saved.",
            "a memory, a delay, then a ramp time");
}

std::uint64_t dDelay::Pack(float value, bool ms) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return (static_cast<std::uint64_t>(ms ? 1u : 0u) << 32) | bits;
}

void dDelay::SetTime(float value, bool ms) {
  if (std::isfinite(value)) time = Pack(value, ms);
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the line may be allocated here. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  time = Pack(0.f, false);
  rampMs = 0.f;
  if (fn.memory() != DSP::sampleDelay::DEFAULT_MEMORY || fn.maximumDelay() != fn.memory()) {
    fn = DSP::sampleDelay();
  }
}

PARM_PARSE() {
  float values[3] = {static_cast<float>(DSP::sampleDelay::DEFAULT_MEMORY), 0.f, 0.f};
  int count = 0;
  for (const std::string& token : arguments) {
    if (count == 3) break;
    float number = 0.f;
    if (ReadNumber(token, number)) values[count++] = number;
  }
  const float memory =
      std::fmin(std::fmax(values[0], 0.f), static_cast<float>(DSP::sampleDelay::MAX_MEMORY));
  const auto samples = static_cast<UInt>(memory);
  if (samples != fn.memory() || fn.maximumDelay() != fn.memory()) fn = DSP::sampleDelay(samples);
  time = Pack(values[1], false);
  rampMs = values[2];
}

FLOAT_IN(SetDelay) {
  (void)inlet;
  (void)thread;
  SetTime(value, false);
}

INT_IN(SetDelayInt) {
  (void)inlet;
  (void)thread;
  SetTime(static_cast<float>(value), false);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  float number = 0.f;
  if (word == "maxsize") {
    if (ReadNumber(Token(message, next, next), number)) {
      const float clamped =
          std::fmin(std::fmax(number, 0.f), static_cast<float>(DSP::sampleDelay::MAX_MEMORY));
      pendingMaximum = static_cast<std::uint32_t>(clamped);
      maximumRequested = true;
    }
    return;
  }
  if (word == "ramp") {
    if (ReadNumber(Token(message, next, next), number) && number >= 0.f) rampMs = number;
    return;
  }
  bool ms = false;
  // Max's attribute, as a message, then a bare time value.
  if (word == "delay") {
    if (ReadTime(message, next, number, ms)) SetTime(number, ms);
    return;
  }
  if (ReadTime(message, 0, number, ms)) SetTime(number, ms);
}

CALC() {
  // A new maximum clears the line, so a clear in the same block is done.
  const bool clear = clearRequested.exchange(false);
  if (maximumRequested.exchange(false)) {
    fn.maximumDelay(pendingMaximum.load());
  } else if (clear) {
    fn.clear();
  }

  const std::uint64_t packed = time.load();
  float delayTime = 0.f;
  const auto bits = static_cast<std::uint32_t>(packed & 0xffffffffu);
  std::memcpy(&delayTime, &bits, sizeof(delayTime));
  if ((packed >> 32) != 0)
    fn.delayMs(delayTime);
  else
    fn.delay(delayTime);
  fn.ramp(rampMs.load());

  // Unconnected inlets (#1036): the input reads 0; the delay inlet takes the
  // number path, which crossfades, unless a signal arrived.
  DSP::buffer& in = inputs[0].Signal(0.f);
  DSP::buffer& out = inputs[1].IsConstant() ? fn(in) : fn(in, inputs[1].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
