#include "dCascade.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <algorithm>
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

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }
} // namespace

#define className dCascade

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: messages (coefficient lists and commands)
  ADD_IN_1;

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Cascade of biquad filters in series, each section y(n) = a0 x(n) + a1 x(n-1) "
                  "+ a2 x(n-2) - b1 y(n-1) - b2 y(n-2), set by a list of coefficients in sets of "
                  "five: higher-order filters built from second-order sections.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). A list of coefficients in sets of five "
            "(a0 a1 a2 b1 b2 per section) sets the sections; 'clear' sets every coefficient and "
            "the previous inputs and outputs to 0; 'bypass 1' passes the input unfiltered; "
            "'zero 1' silences the output; 'smooth 0' or 'smooth 1' turns the smoothing of "
            "coefficient changes off or on.",
            "");
  INLET_DOC(1, "coefficients",
            "A list of coefficients in sets of five, a0 a1 a2 b1 b2 per section, the first five "
            "for the first section; an incomplete last set is ignored. Takes the same words as "
            "the left inlet.",
            "up to the number of sections of the creation argument");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("sections", "16",
            "The number of sections the object has room for (1-64; more coefficient sets than "
            "this in the arguments raise it), then optionally the starting coefficients in sets "
            "of five and 'smooth 0' to turn the smoothing of coefficient changes off "
            "('~cascade 4 0.5 0 0 -0.5 0'). Later lists and messages do not change what is "
            "saved.",
            "a number of sections, then coefficients a0 a1 a2 b1 b2 per section");
}

bool dCascade::Publish(const float* values, unsigned int count, bool clear) {
  // One writer at a time; a second one gives up rather than waits, since
  // it may be the audio thread.
  if (writing.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const std::uint32_t s = sequence.load(std::memory_order_relaxed);
  sequence.store(s + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  count = std::min(count, MAX_VALUES);
  for (unsigned int i = 0; i < count; ++i)
    pending[i].store(values[i], std::memory_order_relaxed);
  pendingCount.store(count, std::memory_order_relaxed);
  sequence.store(s + 2, std::memory_order_release);
  // After the record: a list published before this clear is gone, and the
  // clear is applied before any list published after it.
  if (clear) clearRequested.store(true, std::memory_order_release);
  writing.store(false, std::memory_order_release);
  return true;
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the sections may be allocated here. Clear
// runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  smoothing = true;
  bypassed = false;
  zeroed = false;
  if (fn.capacity() != DSP::biquadCascade::DEFAULT_CAPACITY || fn.sections() != 0)
    fn = DSP::biquadCascade();
}

PARM_PARSE() {
  float values[MAX_VALUES];
  unsigned int count = 0;
  float capacity = static_cast<float>(DSP::biquadCascade::DEFAULT_CAPACITY);
  bool first = true;
  bool smooth = true;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    // smooth takes the number after it; without one it is skipped.
    if (token == "smooth") {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        smooth = number != 0.f;
      }
      continue;
    }
    if (!ReadNumber(token, number)) continue;
    // The first number is the room, the rest the coefficients.
    if (first) {
      capacity = number;
      first = false;
    } else if (count < MAX_VALUES) {
      values[count++] = number;
    }
  }
  // Whole sets only: the integer division is the point.
  const unsigned int wholeSets = count / 5;
  const auto sets = static_cast<float>(wholeSets);
  const auto room =
      static_cast<unsigned int>(std::clamp(std::max(std::round(capacity), sets), 1.f,
                                           static_cast<float>(DSP::biquadCascade::MAX_CAPACITY)));
  if (room != fn.capacity()) fn = DSP::biquadCascade(room);
  smoothing = smooth;
  fn.smooth(smooth);
  fn.coefficients(values, count);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    Publish(nullptr, 0, true);
    return;
  }
  float number = 0.f;
  if (word == "smooth" || word == "bypass" || word == "zero") {
    if (!ReadNumber(Token(message, next, next), number)) return;
    const bool on = number != 0.f;
    if (word == "smooth")
      smoothing = on;
    else if (word == "bypass")
      bypassed = on;
    else
      zeroed = on;
    return;
  }
  // A list of coefficients. A token that is not a finite number stops it;
  // without one complete set of five it changes nothing.
  float values[MAX_VALUES];
  unsigned int count = 0;
  std::string_view token = word;
  while (count < MAX_VALUES && ReadNumber(token, number)) {
    values[count++] = number;
    token = Token(message, next, next);
  }
  if (count < 5) return;
  Publish(values, count, false);
}

CALC() {
  fn.smooth(smoothing.load());
  fn.bypass(bypassed.load());
  fn.zero(zeroed.load());
  if (clearRequested.exchange(false, std::memory_order_acquire)) fn.clear();

  // The pending list, unless it is being written (then the next block).
  const std::uint32_t s = sequence.load(std::memory_order_acquire);
  if ((s & 1u) == 0 && s != applied) {
    float values[MAX_VALUES];
    const unsigned int count = std::min(pendingCount.load(std::memory_order_relaxed), MAX_VALUES);
    for (unsigned int i = 0; i < count; ++i)
      values[i] = pending[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence.load(std::memory_order_relaxed) == s) {
      applied = s;
      fn.coefficients(values, count);
    }
  }

  // Unconnected input (#1036): reads 0.
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
