#include "dSelector.h"

using namespace YSE::PATCHER;

#define className dSelector

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ShapePorts();

  ADD_DESCRIPTION("Passes one of several input signals to its outlet, chosen by a number or a "
                  "signal, or none. Switches hard, or crossfades over a ramp time to avoid "
                  "clicks.");
  ADD_CATEGORY(pCategory::ROUTING);
  PARAM_DOC("arguments", "1 0",
            "The number of signal inputs (1-128) and the input open at the start (0 closes them "
            "all), with 'ramptime <ms>' and 'stepmode <0|1>' among them ('~selector 4 1 "
            "ramptime 10'). Later messages do not change what is saved.",
            "a count, then an input, then the keywords");
}

void dSelector::ShapePorts() {
  // Rebuilt rather than resized: the inlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  const selectorControl::shape shape = control.Parse(arguments);
  fn = DSP::signalSelector(shape.channels, shape.open);

  inputs.clear();
  outputs.clear();

  // in 0: the open input, a number or a signal; next, bang, messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetOpen);
  REG_INT_IN(SetOpenInt);
  REG_BANG_IN(SetNext);
  INLET_DOC(0, "input",
            "The open input, from 1, as a number (truncated) or a signal read per sample; 0 or "
            "less closes every input, above the count opens the last. Unconnected, the last "
            "number sent here. 'next' or bang opens the next input, wrapping; 'ramptime <ms>' "
            "crossfades each switch over that time (0, the default, switches hard); "
            "'stepmode 1' makes each whole-number change of the signal open the next input.",
            "0 or more");

  // in 1 to N: the signals to choose from
  for (UInt i = 0; i < fn.channels(); ++i) {
    inputs.emplace_back(this, false, static_cast<int>(i) + 1);
    REG_SIGNAL_IN;
    const std::string n = std::to_string(i + 1);
    inputs.back().SetDoc("in " + n,
                         "Input " + n +
                             ", passed to the outlet while it is open (unconnected "
                             "reads 0).",
                         "");
  }

  // out 0: the open input
  ADD_OUT_BUFFER;
  OUTLET_DOC(0, "out", "The open input, or silence while every input is closed.", "");
}

PARM_CLEAR() {
  arguments.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

FLOAT_IN(SetOpen) {
  (void)inlet;
  (void)thread;
  control.Number(value);
}

INT_IN(SetOpenInt) {
  (void)inlet;
  (void)thread;
  control.Number(static_cast<Flt>(value));
}

BANG_IN(SetNext) {
  (void)inlet;
  (void)thread;
  control.Next();
}

MESSAGES() {
  (void)value;
  control.Message(message);
}

CALC() {
  // An unconnected control inlet reads its number (#1036); an unconnected
  // input is silent, passed as null so it costs nothing.
  const bool connected = !inputs[0].IsConstant();
  control.Apply(fn, connected);
  for (UInt i = 0; i < fn.channels(); ++i) {
    inlet& in = inputs[i + 1];
    sources[i] = in.IsConstant() ? nullptr : &in.Signal(0.f);
  }
  DSP::buffer& out =
      connected ? fn.select(sources.data(), inputs[0].Signal(0.f)) : fn.select(sources.data());
  outputs[0].SendBuffer(&out, thread);
}

#undef className
