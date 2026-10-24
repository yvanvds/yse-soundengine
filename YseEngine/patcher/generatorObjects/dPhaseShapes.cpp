#include "dPhaseShapes.h"
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

// ─── ~triangle ────────────────────────────────────────────────────────────────

#define className dTriangle

CONSTRUCT_DSP() {
  phase = 0.f;
  peak = 0.5f;
  lo = -1.f;
  hi = 1.f;

  // in 0: the phase (signal, float or int), `lo <x>` and `hi <x>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // in 1: the peak position (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPeak);
  REG_INT_IN(SetPeakInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(peak);
  ADD_PARAM(lo);
  ADD_PARAM(hi);

  ADD_DESCRIPTION("A triangle read by a 0-1 phase such as a ~phasor: it rises from lo to hi at "
                  "the peak position, then falls back to lo. A peak of 0.5 is symmetric, 0 a "
                  "falling ramp, 1 a rising sawtooth. For LFO shapes and envelopes.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The phase, wrapped into 0-1: a signal, or a number used while no signal is "
            "connected. 'lo <x>' and 'hi <x>' set the output range (not saved).",
            "0-1");
  INLET_DOC(1, "peak",
            "Where in the cycle the peak is: a signal, or a number used while no signal is "
            "connected. Held within 0-1.",
            "0-1");
  OUTLET_DOC(0, "out", "The triangle, from lo to hi.", "lo to hi");
  PARAM_DOC("peak", "0.5", "Peak position used while no signal is connected.", "0-1");
  PARAM_DOC("lo", "-1", "Output value at the start and end of the cycle.", "any float");
  PARAM_DOC("hi", "1", "Output value at the peak.", "any float");
}

FLOAT_IN(SetPhase) {
  phase = value;
}

INT_IN(SetPhaseInt) {
  phase = static_cast<float>(value);
}

FLOAT_IN(SetPeak) {
  peak = value;
}

INT_IN(SetPeakInt) {
  peak = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  float number = 0.f;
  if (!ReadWordAndNumber(message, begin, word, number)) return;
  if (Is(message, begin, word, "lo", 2)) {
    lo = number;
  } else if (Is(message, begin, word, "hi", 2)) {
    hi = number;
  }
}

CALC() {
  fn.range(lo, hi);
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(phase);
  DSP::buffer* out = inputs[1].IsConstant() ? &fn(in, peak) : &fn(in, inputs[1].Signal(peak));
  outputs[0].SendBuffer(out, thread);
}

#undef className

// ─── ~trapezoid ───────────────────────────────────────────────────────────────

#define className dTrapezoid

CONSTRUCT_DSP() {
  phase = 0.f;
  up = DSP::trapezoidShape::DEFAULT_UP;
  down = DSP::trapezoidShape::DEFAULT_DOWN;
  lo = 0.f;
  hi = 1.f;
  wrapmode = 1;

  // in 0: the phase (signal, float or int), `lo <x>`, `hi <x>`, `wrapmode <n>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // in 1: the ramp-up point (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetUp);
  REG_INT_IN(SetUpInt);

  // in 2: the ramp-down point (signal, float or int)
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDown);
  REG_INT_IN(SetDownInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(up);
  ADD_PARAM(down);
  ADD_PARAM(lo);
  ADD_PARAM(hi);
  ADD_PARAM(wrapmode);

  ADD_DESCRIPTION("A trapezoid read by a 0-1 phase such as a ~phasor: it rises from lo to hi by "
                  "the ramp-up point, holds hi until the ramp-down point, then falls back to lo. "
                  "For LFO shapes and envelopes.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The phase: a signal, or a number used while no signal is connected. Wrapped into "
            "0-1, or clipped with wrapmode 0. 'lo <x>', 'hi <x>' and 'wrapmode <0|1>' set the "
            "range and the wrap mode (not saved).",
            "0-1");
  INLET_DOC(1, "up",
            "Ramp-up point, where the rise reaches hi: a signal, or a number used while no "
            "signal is connected. Held within 0-1.",
            "0-1");
  INLET_DOC(2, "down",
            "Ramp-down point, where the fall starts: a signal, or a number used while no signal "
            "is connected. Held within 0-1, and no earlier than the ramp-up point.",
            "0-1");
  OUTLET_DOC(0, "out", "The trapezoid, from lo to hi.", "lo to hi");
  PARAM_DOC("up", "0.1", "Ramp-up point used while no signal is connected.", "0-1");
  PARAM_DOC("down", "0.9", "Ramp-down point used while no signal is connected.", "0-1");
  PARAM_DOC("lo", "0", "Output value outside the ramps' tops: the start and end of the cycle.",
            "any float");
  PARAM_DOC("hi", "1", "Output value on the plateau.", "any float");
  PARAM_DOC("wrapmode", "1",
            "1 wraps a phase outside 0-1 to its fractional part, 0 clips it to 0-1.", "0 or 1");
}

FLOAT_IN(SetPhase) {
  phase = value;
}

INT_IN(SetPhaseInt) {
  phase = static_cast<float>(value);
}

FLOAT_IN(SetUp) {
  up = value;
}

INT_IN(SetUpInt) {
  up = static_cast<float>(value);
}

FLOAT_IN(SetDown) {
  down = value;
}

INT_IN(SetDownInt) {
  down = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  float number = 0.f;
  if (!ReadWordAndNumber(message, begin, word, number)) return;
  if (Is(message, begin, word, "lo", 2)) {
    lo = number;
  } else if (Is(message, begin, word, "hi", 2)) {
    hi = number;
  } else if (Is(message, begin, word, "wrapmode", 8)) {
    wrapmode = number != 0.f ? 1 : 0;
  }
}

CALC() {
  fn.range(lo, hi);
  fn.wrap(wrapmode.load() != 0);
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(phase);
  DSP::buffer* out = nullptr;
  if (inputs[1].IsConstant() && inputs[2].IsConstant()) {
    out = &fn(in, up, down);
  } else {
    out = &fn(in, inputs[1].Signal(up), inputs[2].Signal(down));
  }
  outputs[0].SendBuffer(out, thread);
}

#undef className
