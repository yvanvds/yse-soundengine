#include "dFreqshift.h"
#include "../pSelector.h"

#include <string_view>

using namespace YSE::PATCHER;

#define className dFreqshift

CONSTRUCT_DSP() {
  shift = DSP::frequencyShifter::DEFAULT_SHIFT;

  // in 0: the signal to shift; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the shift in Hz, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetShift);
  REG_INT_IN(SetShiftInt);

  // out 0-1: shifted up, shifted down
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(shift);

  ADD_DESCRIPTION("Frequency shifter: moves every frequency of a signal up and down by the same "
                  "number of Hz (not a pitch shift), for inharmonic, metallic sounds and "
                  "barber-pole phasing.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to shift (unconnected reads 0). 'clear' sets the filter state to 0; "
            "'resetphase' sets the oscillator phase to 0.",
            "");
  INLET_DOC(1, "shift",
            "The shift in Hz, a number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument). Negative swaps the outlets. Takes the "
            "same words as the left inlet.",
            "any Hz");
  OUTLET_DOC(0, "up", "The signal with every frequency moved up by the shift.", "");
  OUTLET_DOC(1, "down", "The signal with every frequency moved down by the shift.", "");
  PARAM_DOC("shift", "0",
            "The shift in Hz ('~freqshift 100'). A number in the right inlet changes it, but "
            "what is saved stays the argument.",
            "any Hz");
}

// The DSP class ignores a shift that is not a finite number.
FLOAT_IN(SetShift) {
  (void)inlet;
  (void)thread;
  shift = value;
}

INT_IN(SetShiftInt) {
  (void)inlet;
  (void)thread;
  shift = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // The first word, walked in place so nothing allocates on whichever
  // thread delivers the message.
  const char* chars = message.c_str();
  const std::size_t size = message.size();
  std::size_t begin = 0;
  while (begin < size && IsSelectorSeparator(chars[begin]))
    begin++;
  std::size_t end = begin;
  while (end < size && !IsSelectorSeparator(chars[end]))
    end++;
  const std::string_view word(chars + begin, end - begin);
  if (word == "clear")
    clearRequested = true;
  else if (word == "resetphase")
    resetRequested = true;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();
  if (resetRequested.exchange(false)) fn.resetPhase();

  // Unconnected inlets (#1036): the input reads 0, the shift its number.
  fn(inputs[0].Signal(0.f), inputs[1].Signal(shift.load()));
  // Right outlet first, as Max fires outlets right to left.
  outputs[1].SendBuffer(&fn.down(), thread);
  outputs[0].SendBuffer(&fn.up(), thread);
}

#undef className
