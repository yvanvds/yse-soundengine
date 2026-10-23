#include "dCycle.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dCycle

namespace {
  // The next whitespace-separated word of `message` from `pos`, as
  // [begin, end). Walked in place: no allocation on whichever thread sends.
  void NextWord(const std::string& message, std::size_t pos, std::size_t& begin, std::size_t& end) {
    const std::size_t length = message.size();
    begin = pos;
    while (begin < length && IsSelectorSeparator(message[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(message[end]))
      end++;
  }

  bool IsWord(const std::string& message, std::size_t begin, std::size_t end, const char* word,
              std::size_t length) {
    return end - begin == length && message.compare(begin, length, word) == 0;
  }
} // namespace

CONSTRUCT_DSP() {
  frequency = 0.f;
  phase = 0.f;
  pendingReset = 0.f;

  // in 0: frequency (signal, float or int), and the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: phase (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: the waveform
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(frequency);
  ADD_PARAM(bufferName);
  ADD_PARAM(offset);
  ADD_PARAM(size);

  ADD_DESCRIPTION("A wavetable oscillator: a cosine by default, or one cycle read from a named "
                  "sample buffer. The phase inlet is added to the running phase, for phase "
                  "modulation.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal, or a number used while no signal is connected. Also "
            "takes 'reset [phase]', 'frequency <hz>', 'phase <0-1>', 'buffer_offset <n>' and "
            "'buffer_sizeinsamps <n>'.",
            "any float");
  INLET_DOC(1, "phase",
            "Phase in cycles added to the running phase: a signal for phase modulation, or a "
            "number used while no signal is connected.",
            "0-1");
  OUTLET_DOC(0, "out", "The waveform.", "-1 to 1");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.", "any float");
  PARAM_DOC("buffer", "",
            "Name of a sample buffer whose first channel is the waveform. None uses the built-in "
            "cosine.",
            "buffer name");
  PARAM_DOC("offset", "0", "The first sample of the buffer the waveform starts at.", "0 or higher");
  PARAM_DOC("size", "0",
            "How many buffer samples make up the waveform: 0 is 512, -1 the rest of the buffer.",
            "-1, 0 or higher");
}

// Control thread: the buffer name is a string parameter, so a change of
// argument rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  useBuffer = !bufferName.empty();
  reader.bind(useBuffer ? DSP::sampleBufferRegistry::acquire(bufferName) : nullptr);
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

FLOAT_IN(SetPhase) {
  phase = value;
}

INT_IN(SetPhaseInt) {
  phase = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t end = 0;
  NextWord(message, 0, begin, end);

  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextWord(message, end, argBegin, argEnd);
  float number = 0.f;
  const bool hasNumber = ReadNumericToken(message.c_str() + argBegin, argEnd - argBegin, number);

  if (IsWord(message, begin, end, "reset", 5)) {
    pendingReset = hasNumber ? number : 0.f;
    resetPending = true;
    return;
  }
  if (!hasNumber) return;
  if (IsWord(message, begin, end, "frequency", 9)) {
    frequency = number;
  } else if (IsWord(message, begin, end, "phase", 5)) {
    phase = number;
  } else if (IsWord(message, begin, end, "buffer_offset", 13)) {
    if (number >= 0.f) offset = static_cast<int>(number);
  } else if (IsWord(message, begin, end, "buffer_sizeinsamps", 18)) {
    size = static_cast<int>(number);
  }
}

CALC() {
  if (resetPending.exchange(false)) oscillator.reset(pendingReset);

  // Pinned once for the block; the table points into the view, which lives
  // until the end of CALC().
  DSP::sampleView view = reader.pin();
  if (useBuffer) {
    const int first = offset.load();
    oscillator.table(view, first > 0 ? static_cast<UInt>(first) : 0u, size.load());
  }

  // A connected signal wins over the number (issue #1036).
  const bool freqSignal = !inputs[0].IsConstant();
  const bool phaseSignal = !inputs[1].IsConstant();
  DSP::buffer* out = nullptr;
  if (freqSignal && phaseSignal)
    out = &oscillator(inputs[0].Signal(frequency), inputs[1].Signal(phase));
  else if (freqSignal)
    out = &oscillator(inputs[0].Signal(frequency), phase);
  else if (phaseSignal)
    out = &oscillator(frequency, inputs[1].Signal(phase));
  else
    out = &oscillator(frequency, phase);
  outputs[0].SendBuffer(out, thread);
}

#undef className
