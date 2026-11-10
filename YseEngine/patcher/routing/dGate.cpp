#include "dGate.h"

using namespace YSE::PATCHER;

#define className dGate

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ShapePorts();

  ADD_DESCRIPTION("Sends a signal to one of several outlets, chosen by a number or a signal, or "
                  "to none; the other outlets carry silence. Switches hard, or crossfades over "
                  "a ramp time to avoid clicks.");
  ADD_CATEGORY(pCategory::ROUTING);
  PARAM_DOC("arguments", "1 0",
            "The number of outlets (1-128) and the outlet open at the start (0 closes them "
            "all), with 'ramptime <ms>' and 'stepmode <0|1>' among them ('~gate 4 1 ramptime "
            "10'). Later messages do not change what is saved.",
            "a count, then an outlet, then the keywords");
}

void dGate::ShapePorts() {
  // Rebuilt rather than resized: the outlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  const selectorControl::shape shape = control.Parse(arguments);
  fn = DSP::signalSelector(shape.channels, shape.open);

  inputs.clear();
  outputs.clear();

  // in 0: the open outlet, a number or a signal; next, bang, messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetOpen);
  REG_INT_IN(SetOpenInt);
  REG_BANG_IN(SetNext);
  INLET_DOC(0, "outlet",
            "The open outlet, from 1, as a number (truncated) or a signal read per sample; 0 or "
            "less closes every outlet, above the count opens the last. Unconnected, the last "
            "number sent here. 'next' or bang opens the next outlet, wrapping; 'ramptime <ms>' "
            "crossfades each switch over that time (0, the default, switches hard); "
            "'stepmode 1' makes each whole-number change of the signal open the next outlet.",
            "0 or more");

  // in 1: the signal to route
  ADD_IN_1;
  REG_SIGNAL_IN;
  INLET_DOC(1, "in", "The signal to send to the open outlet (unconnected reads 0).", "");

  for (UInt i = 0; i < fn.channels(); ++i) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc("outlet " + n,
                          "The input while outlet " + n + " is open, silence otherwise.", "");
  }
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
  // An unconnected control inlet reads its number, an unconnected input 0
  // (#1036).
  const bool connected = !inputs[0].IsConstant();
  control.Apply(fn, connected);
  DSP::buffer& in = inputs[1].Signal(0.f);
  if (connected)
    fn.route(in, inputs[0].Signal(0.f));
  else
    fn.route(in);
  for (UInt i = 0; i < fn.channels(); ++i)
    outputs[i].SendBuffer(&fn.output(i), thread);
}

#undef className
