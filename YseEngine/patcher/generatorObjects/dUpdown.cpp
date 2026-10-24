#include "dUpdown.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  // `<word> <number>`, walked in place so nothing allocates on whichever
  // thread delivers the message. False when the message is not one word and
  // a finite number.
  bool ReadWordAndNumber(const std::string& message, std::size_t& begin, std::size_t& word,
                         float& number) {
    const char* text = message.c_str();
    const std::size_t length = message.size();
    begin = 0;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    word = end - begin;

    std::size_t arg = end;
    while (arg < length && IsSelectorSeparator(text[arg]))
      arg++;
    std::size_t argEnd = arg;
    while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
      argEnd++;
    return word > 0 && ReadNumericToken(text + arg, argEnd - arg, number);
  }

  bool Is(const std::string& message, std::size_t begin, std::size_t word, const char* name,
          std::size_t nameLength) {
    return word == nameLength && message.compare(begin, nameLength, name) == 0;
  }
} // namespace

#define className dUpdown

CONSTRUCT_DSP() {
  input = 0.f;
  up = DSP::updown::DEFAULT_UP;
  down = DSP::updown::DEFAULT_DOWN;
  level = DSP::updown::DEFAULT_LEVEL;
  reset = DSP::updown::DEFAULT_RESET;

  // in 0: the ramp or gate (signal, float or int), bang, up / down / level /
  // reset messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Restart);
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(up);
  ADD_PARAM(down);
  ADD_PARAM(level);
  ADD_PARAM(reset);

  ADD_DESCRIPTION("A trapezoid envelope driven by a ramp such as a ~phasor, or by a gate: it "
                  "rises in a fixed time when the ramp starts and falls in a fixed time to end "
                  "at 0 where the ramp ends, however fast the ramp runs. For click-free gating.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "ramp",
            "A 0-1 or 1-0 ramp, or a gate: a signal, or a number used while no signal is "
            "connected. The rise starts when it leaves 0 or jumps by more than reset; while it "
            "is 0 the envelope falls to 0. bang restarts the rise from 0. 'up <ms>', "
            "'down <ms>', 'level <x>' and 'reset <x>' set the attributes (not saved).",
            "any float");
  OUTLET_DOC(0, "out", "The envelope, from 0 to level.", "0 to level");
  PARAM_DOC("up", "10", "Rise time in milliseconds; 0 is instant.", "0 or more");
  PARAM_DOC("down", "10",
            "Fall time in milliseconds, timed to reach 0 where the ramp ends; 0 is instant.",
            "0 or more");
  PARAM_DOC("level", "1", "The envelope's top value.", "any float");
  PARAM_DOC("reset", "0.5", "How far the input must jump between two samples to count as a reset.",
            "0 or more");
}

BANG_IN(Restart) {
  restart = true;
}

FLOAT_IN(SetInput) {
  input = value;
}

INT_IN(SetInputInt) {
  input = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  float number = 0.f;
  if (!ReadWordAndNumber(message, begin, word, number)) return;
  if (Is(message, begin, word, "up", 2)) {
    up = number;
  } else if (Is(message, begin, word, "down", 4)) {
    down = number;
  } else if (Is(message, begin, word, "level", 5)) {
    level = number;
  } else if (Is(message, begin, word, "reset", 5)) {
    reset = number;
  }
}

CALC() {
  fn.up(up).down(down).level(level).resetThreshold(reset);
  if (restart.exchange(false)) fn.restart();
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(input));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
