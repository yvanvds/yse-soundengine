#include "dSah.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

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

  // The number after a one-argument message; false when it is missing or not
  // a number.
  bool Argument(const std::string& message, std::size_t& cursor, float& number) {
    std::size_t begin = 0;
    std::size_t length = 0;
    if (!NextToken(message, cursor, begin, length)) return false;
    return ReadNumericToken(message.c_str() + begin, length, number);
  }

  // The attribute keywords of the arguments and messages.
  enum class keyword { none, thresh, triggermode, duration };

  keyword ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "thresh") return keyword::thresh;
    if (word == "triggermode") return keyword::triggermode;
    if (word == "duration" || word == "dur") return keyword::duration;
    return keyword::none;
  }
} // namespace

#define className dSah

CONSTRUCT_DSP() {
  // in 0: the signal to sample (a number while unconnected); messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);
  REG_LIST_IN(SetList);

  // in 1: the trigger signal; a number sets the threshold
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetThresh);
  REG_INT_IN(SetThreshInt);

  // out 0: the held value
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Samples its input each time a trigger signal crosses a threshold and holds that "
                  "value until the next trigger: classic sample-and-hold modulation, or latching "
                  "one value per step of a ~phasor.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to sample (unconnected, the last number sent here, 0 at first). "
            "'thresh <x>' sets the threshold; 'triggermode <0-6>' (0 rising above the "
            "threshold, 1 falling below it, 2 either, 3 equal to it, 4 from zero to nonzero, 5 "
            "from nonzero to zero, 6 passing it as a phase, so threshold 0 triggers at each reset "
            "of a ~phasor); 'duration <samples>' or 'dur <samples>' (above 0, the held value "
            "lasts that many samples, then 0).",
            "");
  INLET_DOC(1, "trigger",
            "The trigger signal compared with the threshold (unconnected reads 0). A number sets "
            "the threshold.",
            "");
  OUTLET_DOC(0, "out", "The value held since the last trigger.", "");
  PARAM_DOC("thresh", "0",
            "The threshold, the first number. 'thresh <x>', 'triggermode <n>' and "
            "'duration <n>' (or 'dur <n>') among the arguments set those attributes (default 0, "
            "0 and 0). Messages do not change what is saved.",
            "a number, then keywords");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  input = 0.f;
  thresh = 0.f;
  triggermode = DSP::sampleHold::ASCENDING;
  duration = 0;
}

PARM_PARSE() {
  bool haveThreshold = false;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    const keyword word = ToKeyword(token.c_str(), token.size());
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
        ++i;
        SetAttribute(static_cast<int>(word), number);
      }
      continue;
    }
    // The first bare number is the threshold; later ones are ignored.
    if (!haveThreshold && ReadNumericToken(token, number) && std::isfinite(number)) {
      thresh = number;
      haveThreshold = true;
    }
  }
}

void dSah::SetAttribute(int which, float number) {
  switch (static_cast<keyword>(which)) {
  case keyword::thresh:
    if (std::isfinite(number)) thresh = number;
    break;
  case keyword::triggermode:
    if (number >= 0.f && number < 7.f) triggermode = static_cast<int>(number);
    break;
  case keyword::duration:
    if (number >= 0.f && number < 2147483648.f) duration = static_cast<unsigned int>(number);
    break;
  case keyword::none:
    break;
  }
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  input = value;
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  input = static_cast<float>(value);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a command that came as a list, as text through a
  // receive does: the inlet hands every list to a list handler. A list of
  // numbers sets the input to its first.
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  if (!NextToken(value, cursor, begin, length)) return;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) {
    SetMessage(value, 0.f);
    return;
  }
  input = number;
}

FLOAT_IN(SetThresh) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) thresh = value;
}

INT_IN(SetThreshInt) {
  (void)inlet;
  (void)thread;
  thresh = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;
  const keyword word = ToKeyword(message.c_str() + begin, length);
  float number = 0.f;
  if (word == keyword::none || !Argument(message, cursor, number)) return;
  SetAttribute(static_cast<int>(word), number);
}

CALC() {
  fn.threshold(thresh.load())
      .triggerMode(static_cast<DSP::sampleHold::TRIGGER_MODE>(triggermode.load()))
      .duration(duration.load());

  // Unconnected inlets (#1036): the input reads its number, the trigger 0.
  DSP::buffer& out = fn(inputs[0].Signal(input.load()), inputs[1].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
