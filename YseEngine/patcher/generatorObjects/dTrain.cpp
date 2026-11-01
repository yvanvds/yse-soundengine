#include "dTrain.h"
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
} // namespace

#define className dTrain

CONSTRUCT_DSP() {
  interval = 1000.f;
  width = 0.5f;
  phase = 0.f;
  resetmode = 0;

  // in 0: the period in ms (signal, float or int); a bang restarts the
  // cycle; 'interval', 'width', 'phase' and 'resetmode'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Restart);
  REG_FLOAT_IN(SetInterval);
  REG_INT_IN(SetIntervalInt);

  // in 1: the pulse width (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetWidth);
  REG_INT_IN(SetWidthInt);

  // in 2: the phase (signal, float or int)
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: the pulse train; out 1: a bang on each onset, sent after the
  // block (issue #1037)
  ADD_OUT_BUFFER;
  ADD_OUT_BANG;

  ADD_PARAM(interval);
  ADD_PARAM(width);
  ADD_PARAM(phase);
  ADD_PARAM(resetmode);

  ADD_DESCRIPTION("Pulse train switching between 0 and 1, with its period in milliseconds, a "
                  "pulse width and a phase: a clock, gate or rhythmic modulator. Bangs when "
                  "each pulse begins, as a metronome with a fractional interval.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "interval",
            "Period in milliseconds: a signal, or a number used while no signal is connected. 0 "
            "stops the train. A bang restarts the cycle so a pulse begins at the next block. "
            "Also takes 'interval <ms>', 'width <0-1>', 'phase <0-1>' and 'resetmode <0|1>'.",
            "0 or more ms");
  INLET_DOC(1, "width",
            "Pulse width: the part of each period at 1. A signal, or a number used while no "
            "signal is connected. 0 is a single sample, 1 the whole period but one sample.",
            "0-1");
  INLET_DOC(2, "phase",
            "Phase: delays the pulse by this fraction of the period; 1 is the same as 0. A "
            "signal, or a number used while no signal is connected.",
            "0-1");
  OUTLET_DOC(0, "out", "The pulse train: 1 during each pulse, 0 between them.", "0 or 1");
  OUTLET_DOC(1, "onset", "A bang after a block in which a pulse began.", "bang");
  PARAM_DOC("interval", "1000", "Period in milliseconds used while no signal is connected.",
            "0 or more ms");
  PARAM_DOC("width", "0.5", "Pulse width used while no signal is connected.", "0-1");
  PARAM_DOC("phase", "0", "Phase used while no signal is connected.", "0-1");
  PARAM_DOC("resetmode", "0",
            "1 holds a new interval until the running period ends; 0 applies it at once.",
            "0 or 1");
}

BANG_IN(Restart) {
  (void)inlet;
  (void)thread;
  restart = true;
}

FLOAT_IN(SetInterval) {
  (void)inlet;
  (void)thread;
  interval = value;
}

INT_IN(SetIntervalInt) {
  (void)inlet;
  (void)thread;
  interval = static_cast<float>(value);
}

FLOAT_IN(SetWidth) {
  (void)inlet;
  (void)thread;
  width = value;
}

INT_IN(SetWidthInt) {
  (void)inlet;
  (void)thread;
  width = static_cast<float>(value);
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  phase = value;
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  phase = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // 'interval <ms>', 'width <0-1>', 'phase <0-1>' and 'resetmode <0|1>'; a
  // missing or malformed number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word != "interval" && word != "width" && word != "phase" && word != "resetmode") return;
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!ReadNumericToken(token.data(), token.size(), number) || std::isnan(number)) return;
  if (word == "interval")
    interval = number;
  else if (word == "width")
    width = number;
  else if (word == "phase")
    phase = number;
  else
    resetmode = number != 0.f ? 1 : 0;
}

CALC() {
  // The DSP class cleans the values: a negative interval stops, the width
  // is held in 0..1, the phase wraps.
  fn.interval(interval.load());
  fn.width(width.load());
  fn.phase(phase.load());
  fn.deferInterval(resetmode.load() != 0);
  if (restart.exchange(false)) fn.trigger();
  // A connected signal wins over the number (issue #1036).
  DSP::buffer& out = fn(inputs[0].Signal(fn.interval()), inputs[1].Signal(fn.width()),
                        inputs[2].Signal(fn.phase()));
  // Never Send* a control message from CALC(): the patcher sends it after
  // the traversal.
  if (fn.onsets() > 0) outputs[1].ArmBang();
  outputs[0].SendBuffer(&out, thread);
}

#undef className
