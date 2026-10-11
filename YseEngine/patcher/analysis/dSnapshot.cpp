#include "dSnapshot.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../headers/constants.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className dSnapshot

CONSTRUCT_DSP() {
  interval = 0.f;
  offset = 0;

  // in 0: the signal, bang, on/off, and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);
  REG_FLOAT_IN(SetActive);
  REG_INT_IN(SetActiveInt);

  // in 1: the interval in ms
  ADD_IN_1;
  REG_FLOAT_IN(SetInterval);
  REG_INT_IN(SetIntervalInt);

  // out 0: the value, sent after the block (issue #1037) or on a bang
  ADD_OUT_FLOAT;

  ADD_PARAM(interval);
  ADD_PARAM(offset);

  ADD_DESCRIPTION("Reports the value of a signal as a number: on a bang, or every interval while "
                  "reporting is on.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to sample. A bang reports the current value; a non-zero number starts "
            "interval reporting and 0 stops it. Also takes 'start', 'stop', 'interval <ms>', "
            "'sampleinterval <samples>' and 'offset <index>'.",
            "any float");
  INLET_DOC(1, "interval", "Reporting interval in milliseconds. 0 stops interval reporting.",
            "0 or more ms");
  OUTLET_DOC(0, "value", "The sampled value.", "any float");
  PARAM_DOC("interval", "0",
            "Reporting interval in milliseconds. 0 reports only on a bang. An interval shorter "
            "than a block reports once per block.",
            "0 or more ms");
  PARAM_DOC("offset", "0", "Which sample of the block is reported.", "0-127");
}

// A bang is a message, not CALC(), so it may send at once: the value from the
// last rendered block.
BANG_IN(Report) {
  outputs[0].SendFloat(lastValue.load(), thread);
}

FLOAT_IN(SetActive) {
  active = value != 0.f;
}

INT_IN(SetActiveInt) {
  active = value != 0;
}

FLOAT_IN(SetInterval) {
  interval = value < 0.f ? 0.f : value;
  sampleInterval = 0;
}

INT_IN(SetIntervalInt) {
  interval = value < 0 ? 0.f : static_cast<float>(value);
  sampleInterval = 0;
}

// `start`, `stop`, `interval <ms>`, `sampleinterval <n>` and `offset <n>`.
// Walked in place, as inlet.cpp does, so this allocates nothing.
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

  if (word == 5 && message.compare(begin, 5, "start") == 0) {
    active = true;
    return;
  }
  if (word == 4 && message.compare(begin, 4, "stop") == 0) {
    active = false;
    return;
  }

  std::size_t arg = end;
  while (arg < length && IsSelectorSeparator(text[arg]))
    arg++;
  std::size_t argEnd = arg;
  while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
    argEnd++;
  float number = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, number)) return;

  if (word == 8 && message.compare(begin, 8, "interval") == 0) {
    interval = number < 0.f ? 0.f : number;
    sampleInterval = 0;
  } else if (word == 14 && message.compare(begin, 14, "sampleinterval") == 0) {
    sampleInterval = number < 1.f ? 0 : static_cast<int>(number);
  } else if (word == 6 && message.compare(begin, 6, "offset") == 0) {
    offset = number < 0.f ? 0 : static_cast<int>(number);
  }
}

CALC() {
  (void)thread;
  DSP::buffer& in = inputs[0].Signal(0.f);
  const int length = static_cast<int>(in.getLength());
  if (length == 0) return;
  int index = offset.load();
  if (index >= length) index = length - 1;
  const float sample = in.getPtr()[index];
  lastValue = sample;

  // The period follows the live sample rate, so it is worked out every block.
  const int samples = sampleInterval.load();
  const double period = samples > 0
                            ? static_cast<double>(samples)
                            : static_cast<double>(interval.load()) * YSE::SAMPLERATE / 1000.0;
  if (!active.load() || period <= 0.0) {
    // Report on the first block after reporting (re)starts.
    samplesLeft = 0.0;
    return;
  }

  // samplesLeft counts from the start of this block to the next report.
  if (samplesLeft < length) {
    // Never Send* from CALC(): the patcher sends this after the traversal.
    outputs[0].ArmFloat(sample);
    // Skip to the first report point past this block: several due in one
    // block (a period shorter than a block) are one report.
    samplesLeft += std::ceil((length - samplesLeft) / period) * period;
  }
  samplesLeft -= length;
}

#undef className
