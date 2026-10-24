#include "dRamp.h"
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

#define className dRamp

CONSTRUCT_DSP() {
  duration = DSP::triggeredRamp::DEFAULT_DURATION;
  start = DSP::triggeredRamp::DEFAULT_START;
  end = DSP::triggeredRamp::DEFAULT_END;
  curve = 0.f;

  // in 0: the trigger (signal); float / int set the duration; attribute
  // messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1-3: duration, start and end (signal, or a number while unconnected)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(duration);
  ADD_PARAM(start);
  ADD_PARAM(end);
  ADD_PARAM(curve);
  ADD_PARAM(mode);
  ADD_PARAM(reset);
  ADD_PARAM(retrigger);

  ADD_DESCRIPTION("A single ramp from a start to an end value, started sample-accurately when a "
                  "trigger signal goes from 0 to non-zero. For one-shot envelopes driven at audio "
                  "rate.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "trigger",
            "A signal; a ramp starts when it goes from 0 to non-zero (unconnected reads 0). A "
            "number sets the duration. 'duration <ms>', 'start <x>', 'end <x>', 'curve <x>', "
            "'mode <0|1>', 'reset <0|1>' and 'retrigger <0|1>' set the attributes (not saved); "
            "'interval <n>' is accepted and ignored.",
            "any float");
  INLET_DOC(1, "duration",
            "The ramp's length in milliseconds: a signal, or a number used while no signal is "
            "connected.",
            "0 or more ms");
  INLET_DOC(2, "start",
            "The value the ramp starts from: a signal, or a number used while no signal is "
            "connected.",
            "any float");
  INLET_DOC(3, "end",
            "The value the ramp ends on: a signal, or a number used while no signal is "
            "connected.",
            "any float");
  OUTLET_DOC(0, "out", "The ramp.", "start to end");
  PARAM_DOC("duration", "100", "Ramp length in milliseconds, used while inlet 1 is unconnected.",
            "0 or more");
  PARAM_DOC("start", "0", "Start value, used while inlet 2 is unconnected.", "any float");
  PARAM_DOC("end", "1", "End value, used while inlet 3 is unconnected.", "any float");
  PARAM_DOC("curve", "0",
            "Curvature: 0 is linear; below 0 a rising ramp runs above the straight line, above 0 "
            "below it.",
            "-1 to 1");
  PARAM_DOC("mode", "0",
            "0: the duration is read when the ramp starts; 1: it is read continuously and a "
            "change stretches what is left of the ramp.",
            "0-1");
  PARAM_DOC("reset", "0", "1: the output returns to 0 when the ramp ends; 0: it holds the end.",
            "0-1");
  PARAM_DOC("retrigger", "0", "1: a trigger restarts a running ramp; 0: it is ignored.", "0-1");
}

void dRamp::Set(int inlet, float value) {
  switch (inlet) {
  case 2:
    start = value;
    break;
  case 3:
    end = value;
    break;
  default: // Max: a number in the left or second inlet is the duration
    duration = value;
    break;
  }
}

FLOAT_IN(SetFloat) {
  (void)thread;
  Set(inlet, value);
}

INT_IN(SetInt) {
  (void)thread;
  Set(inlet, static_cast<float>(value));
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  float number = 0.f;
  if (!ReadWordAndNumber(message, begin, word, number)) return;
  if (Is(message, begin, word, "duration", 8)) {
    duration = number;
  } else if (Is(message, begin, word, "start", 5)) {
    start = number;
  } else if (Is(message, begin, word, "end", 3)) {
    end = number;
  } else if (Is(message, begin, word, "curve", 5)) {
    curve = number;
  } else if (Is(message, begin, word, "mode", 4)) {
    mode = static_cast<int>(number);
  } else if (Is(message, begin, word, "reset", 5)) {
    reset = static_cast<int>(number);
  } else if (Is(message, begin, word, "retrigger", 9)) {
    retrigger = static_cast<int>(number);
  }
  // "interval <n>": Max's segment length for its curve approximation; the
  // DSP class computes the curve exactly, so there is nothing to set.
}

CALC() {
  fn.duration(duration)
      .start(start)
      .end(end)
      .curve(curve)
      .mode(mode != 0 ? DSP::triggeredRamp::durationMode::continuous
                      : DSP::triggeredRamp::durationMode::atStart)
      .resetToZero(reset != 0)
      .retrigger(retrigger != 0);
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(duration), inputs[2].Signal(start),
                        inputs[3].Signal(end));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
