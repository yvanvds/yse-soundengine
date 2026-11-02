#include "dMinmax.h"
#include "../pSelector.h"

#include <string_view>

using namespace YSE::PATCHER;

#define className dMinmax

CONSTRUCT_DSP() {
  // in 0: the signal (signal, float or int); a bang reports; 'reset'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);
  REG_FLOAT_IN(SetIn);
  REG_INT_IN(SetInInt);

  // in 1: the reset signal (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetReset);
  REG_INT_IN(SetResetInt);

  // out 0, 1: the running minimum and maximum, as signals
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  // out 2, 3: the minimum and maximum on a bang, sent after the block (#1037)
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Tracks the running minimum and maximum of a signal: the lowest and highest "
                  "sample values since the last reset, as signals and on a bang.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to measure, or a number used while no signal is connected. A bang "
            "reports the minimum and maximum so far; 'reset' starts over.",
            "any float");
  INLET_DOC(1, "reset",
            "The reset signal, or a number used while none is connected: every sample where "
            "it is not 0 starts over at that sample.",
            "any float");
  OUTLET_DOC(0, "minimum",
             "The lowest sample value since the last reset, after each sample. 0 until the "
             "first.",
             "any float");
  OUTLET_DOC(1, "maximum",
             "The highest sample value since the last reset, after each sample. 0 until the "
             "first.",
             "any float");
  OUTLET_DOC(2, "minimum on bang", "The lowest sample value since the last reset, on a bang.",
             "any float");
  OUTLET_DOC(3, "maximum on bang", "The highest sample value since the last reset, on a bang.",
             "any float");
}

BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  report = true;
}

FLOAT_IN(SetIn) {
  (void)inlet;
  (void)thread;
  in = value;
}

INT_IN(SetInInt) {
  (void)inlet;
  (void)thread;
  in = static_cast<float>(value);
}

FLOAT_IN(SetReset) {
  (void)inlet;
  (void)thread;
  resetIn = value;
}

INT_IN(SetResetInt) {
  (void)inlet;
  (void)thread;
  resetIn = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // 'reset', walked in place so nothing allocates on whichever thread
  // delivers the message.
  const char* chars = message.c_str();
  const std::size_t size = message.size();
  std::size_t begin = 0;
  while (begin < size && IsSelectorSeparator(chars[begin]))
    begin++;
  std::size_t end = begin;
  while (end < size && !IsSelectorSeparator(chars[end]))
    end++;
  if (std::string_view(chars + begin, end - begin) == "reset") resetRequested = true;
}

CALC() {
  // Never Send* floats from CALC(): the patcher sends these after the
  // traversal. A bang reports up to the end of the last block, then a reset
  // that came with it starts over.
  if (report.exchange(false)) {
    outputs[3].ArmFloat(fn.maximum());
    outputs[2].ArmFloat(fn.minimum());
  }
  if (resetRequested.exchange(false)) fn.reset();
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  fn(inputs[0].Signal(in.load()), inputs[1].Signal(resetIn.load()));
  // Right to left, as Max sends.
  outputs[1].SendBuffer(&fn.maximumSignal(), thread);
  outputs[0].SendBuffer(&fn.minimumSignal(), thread);
}

#undef className
