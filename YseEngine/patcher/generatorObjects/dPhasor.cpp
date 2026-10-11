#include "dPhasor.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dPhasor

CONSTRUCT_DSP() {
  frequency = 0.f;
  phaseoffset = 0.f;
  pendingPhase = 0.f;

  // in 0: frequency (signal, float or int), and the reset / phaseoffset messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: phase jump (float or int)
  ADD_IN_1;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: the ramp
  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
  ADD_PARAM(phaseoffset);

  ADD_DESCRIPTION("A ramp from 0 to 1 that wraps around at a given frequency: the phase source "
                  "for wavetable reads, sequencers and sawtooth shapes. A negative frequency runs "
                  "it backwards.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal, or a number used while no signal is connected. Also "
            "takes 'reset' (jump to phaseoffset) and 'phaseoffset <0-1>'.",
            "any float");
  INLET_DOC(1, "phase", "Jumps to this phase now. Does not change phaseoffset.", "0-1");
  OUTLET_DOC(0, "out", "The phase ramp.", "0-1");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.", "any float");
  PARAM_DOC("phaseoffset", "0", "The phase the ramp starts at, and returns to on 'reset'.", "0-1");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

FLOAT_IN(SetPhase) {
  pendingPhase = value;
  phasePending = true;
}

INT_IN(SetPhaseInt) {
  pendingPhase = static_cast<float>(value);
  phasePending = true;
}

// `reset` and `phaseoffset <x>`. The text is walked in place, as inlet.cpp
// does, so this allocates nothing on whichever thread delivers it.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  const std::size_t word = end - begin;

  if (word == 5 && message.compare(begin, 5, "reset") == 0) {
    resetPending = true;
    return;
  }
  if (word == 11 && message.compare(begin, 11, "phaseoffset") == 0) {
    std::size_t arg = end;
    while (arg < length && IsSelectorSeparator(text[arg]))
      arg++;
    std::size_t argEnd = arg;
    while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
      argEnd++;
    float number = 0.f;
    if (ReadNumericToken(text + arg, argEnd - arg, number)) phaseoffset = number;
  }
}

CALC() {
  if (!started) {
    phasor.phase(phaseoffset);
    started = true;
  }
  // A reset and a phase jump in the same block: the jump was sent last only
  // if it arrived after the reset, which the flags cannot tell, so the
  // explicit phase wins.
  if (resetPending.exchange(false)) phasor.phase(phaseoffset);
  if (phasePending.exchange(false)) phasor.phase(pendingPhase);

  // A connected frequency signal wins over the float (issue #1036).
  DSP::buffer& out =
      inputs[0].IsConstant() ? phasor(frequency) : phasor(inputs[0].Signal(frequency));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
