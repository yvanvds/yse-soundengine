#include "selectorControl.h"
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

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }

  // The attribute keywords of the arguments and messages. `ramp` is
  // ramptime's alias.
  enum class keyword { none, ramptime, stepmode };

  keyword ToKeyword(std::string_view word) {
    if (word == "ramptime" || word == "ramp") return keyword::ramptime;
    if (word == "stepmode") return keyword::stepmode;
    return keyword::none;
  }
} // namespace

selectorControl::shape selectorControl::Parse(const std::vector<std::string>& arguments) {
  shape result;
  ramp.store(0.f, std::memory_order_relaxed);
  step.store(false, std::memory_order_relaxed);

  float numbers[2] = {static_cast<float>(DSP::signalSelector::DEFAULT_CHANNELS), 0.f};
  unsigned int position = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    float number = 0.f;
    const keyword word = ToKeyword(arguments[i]);
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        if (word == keyword::ramptime)
          ramp.store(number < 0.f ? 0.f : number, std::memory_order_relaxed);
        else
          step.store(number != 0.f, std::memory_order_relaxed);
      }
      continue;
    }
    if (position < 2 && ReadNumber(arguments[i], number)) numbers[position++] = number;
  }

  const float count = std::round(numbers[0]);
  result.channels =
      count < 1.f ? 1u
                  : static_cast<UInt>(
                        std::fmin(count, static_cast<float>(DSP::signalSelector::MAX_CHANNELS)));
  result.open = DSP::signalSelector::Channel(numbers[1], result.channels);
  channels = result.channels;
  number.store(result.open, std::memory_order_relaxed);
  return result;
}

void selectorControl::Number(Flt value) {
  number.store(DSP::signalSelector::Channel(value, channels), std::memory_order_relaxed);
}

void selectorControl::Next() {
  number.store(DSP::signalSelector::Next(number.load(std::memory_order_relaxed), channels),
               std::memory_order_relaxed);
}

bool selectorControl::Message(const std::string& message) {
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float value = 0.f;
  if (word == "next") {
    Next();
    return true;
  }
  const keyword attribute = ToKeyword(word);
  if (attribute == keyword::ramptime) {
    if (ReadNumber(Token(message, next, next), value))
      ramp.store(value < 0.f ? 0.f : value, std::memory_order_relaxed);
    return true;
  }
  if (attribute == keyword::stepmode) {
    if (ReadNumber(Token(message, next, next), value))
      step.store(value != 0.f, std::memory_order_relaxed);
    return true;
  }
  // A number, or a list that starts with one: the first number.
  if (!ReadNumber(word, value)) return false;
  Number(value);
  return true;
}

void selectorControl::Apply(DSP::signalSelector& fn, bool controlConnected) const {
  fn.rampTime(ramp.load(std::memory_order_relaxed)).stepMode(step.load(std::memory_order_relaxed));
  if (!controlConnected) fn.open(number.load(std::memory_order_relaxed));
}
