#include "dOscbank.h"

using namespace YSE::PATCHER;

#define className oscillatorBankObject

namespace {
  constexpr int MAX_OSCILLATORS = static_cast<int>(oscillatorBankControl::MAX_OSCILLATORS);

  UInt AtLeast(int value, int least) {
    return static_cast<UInt>(value < least ? least : value);
  }
} // namespace

oscillatorBankObject::oscillatorBankObject(bool interpolating)
  : pObject(true),
    interpolating(interpolating),
    bank(DSP::oscillatorBank::DEFAULT_OSCILLATORS, interpolating),
    control(!interpolating) {
  // in 0: frequency; in 1: magnitude; in 2: phase; in 3: the oscillator
  // they set. Each a signal, a float or an int. Messages on any inlet.
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
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

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(oscillators);
  ADD_PARAM(freqsmooth);
  ADD_PARAM(magsmooth);
  if (!interpolating) ADD_PARAM(tablesize);

  ADD_DESCRIPTION(interpolating
                      ? "A bank of sine oscillators with smoothed frequency and magnitude "
                        "changes, summed into one signal, reading its table between points for a "
                        "cleaner sine: additive synthesis, and resynthesis from analysis data."
                      : "A bank of sine oscillators with smoothed frequency and magnitude "
                        "changes, summed into one signal, reading its table without "
                        "interpolation (cheaper; set the table size with tabpoints): additive "
                        "synthesis, and resynthesis from analysis data.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz of the oscillator the index names. A signal sets it sample by "
            "sample; a number sets it once while no inlet carries a signal. Also takes 'set f m "
            "f m ...', 'clear', 'silence', 'size <n>', 'freqsmooth <n>', 'magsmooth <n>' and "
            "'framesync <0/1>'" +
                std::string(interpolating ? "." : ", and 'tabpoints <n>'."),
            "any float");
  INLET_DOC(1, "magnitude",
            "Magnitude (amplitude) of the oscillator the index names: a signal, or a number.",
            "0-1");
  INLET_DOC(2, "phase",
            "With framesync 1 only: the phase the oscillator the index names jumps to when its "
            "frame takes effect. A signal, or a number.",
            "0-1");
  INLET_DOC(3, "index",
            "The oscillator the other inlets set, from 0. A signal (one oscillator per sample, "
            "as an analysis frame arrives), or a number naming the oscillator later numbers set.",
            "0 to oscillators - 1");
  OUTLET_DOC(0, "out", "The sum of every oscillator: magnitude times sine.", "any float");
  PARAM_DOC("oscillators", "64",
            "How many oscillators the bank allocates; 'size' runs fewer. Changing it rebuilds "
            "the object.",
            "1-4096");
  PARAM_DOC("freqsmooth", "1", "Samples over which a new frequency is ramped to; 0 or 1 jumps.",
            "0 or more");
  PARAM_DOC("magsmooth", "0", "Samples over which a new magnitude is ramped to; 0 or 1 jumps.",
            "0 or more");
  if (!interpolating)
    PARAM_DOC("tablesize", "4096",
              "Points in the sine table, rounded to a power of two: more is cleaner.", "2-65536");
}

// Control thread: the callbacks make ParamsNeedRebuild() true, so a live
// change rebuilds the object and the allocation never happens on a bank the
// audio thread is rendering. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  oscillators = static_cast<int>(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
  freqsmooth = 1;
  magsmooth = 0;
  tablesize = static_cast<int>(DSP::oscillatorBank::DEFAULT_TABLE_SIZE);
  bank = DSP::oscillatorBank(DSP::oscillatorBank::DEFAULT_OSCILLATORS, interpolating);
  control.Build(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
}

PARM_PARSE() {
  if (oscillators < 1) oscillators = 1;
  if (oscillators > MAX_OSCILLATORS) oscillators = MAX_OSCILLATORS;
  const UInt capacity = AtLeast(oscillators, 1);
  bank = DSP::oscillatorBank(capacity, interpolating,
                             interpolating ? DSP::oscillatorBank::DEFAULT_TABLE_SIZE
                                           : AtLeast(tablesize, 0));
  bank.frequencySmoothing(AtLeast(freqsmooth, 0));
  bank.magnitudeSmoothing(AtLeast(magsmooth, 0));
  control.Build(capacity);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  switch (inlet) {
  case 0:
    control.Frequency(value);
    break;
  case 1:
    control.Magnitude(value);
    break;
  case 2:
    control.Phase(value);
    break;
  default:
    control.Index(value);
    break;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  control.Message(message);
}

CALC() {
  const bool signals = !inputs[0].IsConstant() || !inputs[1].IsConstant() ||
                       !inputs[2].IsConstant() || !inputs[3].IsConstant();
  control.Apply(bank, signals);
  DSP::buffer* out = nullptr;
  if (signals) {
    // Unconnected inlets read as their numbers (#1036); the phase only when
    // a phase signal is connected, so frames do not reset running phases.
    DSP::buffer& frequency = inputs[0].Signal(control.FrequencyNumber());
    DSP::buffer& magnitude = inputs[1].Signal(control.MagnitudeNumber());
    DSP::buffer& index = inputs[3].Signal(control.IndexNumber());
    out = inputs[2].IsConstant() ? &bank(frequency, magnitude, index)
                                 : &bank(frequency, magnitude, inputs[2].Signal(0.f), index);
  } else {
    out = &bank();
  }
  outputs[0].SendBuffer(out, thread);
}

#undef className
