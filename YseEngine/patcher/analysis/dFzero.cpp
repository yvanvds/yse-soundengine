#include "dFzero.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <cstring>

using namespace YSE::PATCHER;
using YSE::DSP::pitchTracker;

namespace {
  // `<word> <number>`, walked in place so nothing allocates on whichever
  // thread delivers the message. False when the message is not a word and
  // a number.
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
    return word > 0 && ReadNumericToken(text + arg, argEnd - arg, number) && !std::isnan(number);
  }

  bool Is(const std::string& message, std::size_t begin, std::size_t word, const char* name) {
    const std::size_t length = std::strlen(name);
    return word == length && message.compare(begin, length, name) == 0;
  }

  // A count of samples as the class takes it: a negative number is 0, a
  // huge one is held to what an int holds.
  int Samples(float value) {
    if (!(value > 0.f)) return 0;
    if (value >= 2147483520.f) return 2147483520;
    return static_cast<int>(value);
  }

  void AppendNumber(std::string& out, float value) {
    char number[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    if (written > 0) out.append(number, static_cast<std::size_t>(written));
  }
} // namespace

#define className dFzero

CONSTRUCT_DSP() {
  // in 0: the signal; attribute messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // out 0 - 2: pitch, peak amplitude, onset; sent after the block (#1037)
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;
  ADD_OUT_ANY;

  ADD_PARAM(size);
  ADD_PARAM(period);
  ADD_PARAM(freqmin);
  ADD_PARAM(freqmax);
  ADD_PARAM(threshold);
  ADD_PARAM(onsetamp);
  ADD_PARAM(onsetpitch);
  ADD_PARAM(onsetperiod);
  ADD_PARAM(onsetlist);
  ADD_PARAM(tolerance);

  // "<pitch> <amplitude>": two numbers of at most kExprValueTextMax each
  scratch.reserve(2 * kExprValueTextMax + 2);

  ADD_DESCRIPTION("Estimates the fundamental frequency of a monophonic signal with the YIN "
                  "method, every period samples over the last size samples, and reports the "
                  "pitch in Hz, the peak amplitude and the onsets of new notes.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to analyse; unconnected it reads as silence. Takes 'size <n>', "
            "'period <n>', 'freqmin <hz>', 'freqmax <hz>', 'threshold <x>', 'onsetamp <x>', "
            "'onsetpitch <semitones>', 'onsetperiod <n>', 'onsetlist <0/1>' and "
            "'tolerance <x>' (not saved).",
            "any float");
  OUTLET_DOC(0, "pitch",
             "The estimated fundamental frequency in Hz, after each analysis that found one "
             "within freqmin to freqmax and above the threshold.",
             "freqmin to freqmax Hz");
  OUTLET_DOC(1, "amplitude",
             "The peak amplitude of the analysis buffer, after each analysis at or above the "
             "threshold.",
             "threshold or more");
  OUTLET_DOC(2, "onset",
             "A bang when a new note starts: the peak amplitude rises to onsetamp, or the pitch "
             "moves onsetpitch semitones from the note's. With onsetlist 1, a list of the pitch "
             "in Hz (0 when none was found yet) and the peak amplitude instead.",
             "bang, or a list of two floats");
  PARAM_DOC("size", "2048",
            "Samples in each analysis, rounded down to a power of two from 128 to 4096. The "
            "lowest pitch it finds is the sample rate divided by half of it, less one.",
            "128 to 4096");
  PARAM_DOC("period", "512", "Samples between two analyses.", "16 to 4096");
  PARAM_DOC("freqmin", "40", "The lowest pitch reported, in Hz.", "above 0");
  PARAM_DOC("freqmax", "4000", "The highest pitch reported, in Hz.", "above 0");
  PARAM_DOC("threshold", "0.01", "An analysis whose peak amplitude is below this reports nothing.",
            "0 or more");
  PARAM_DOC("onsetamp", "0.1",
            "A rise of the peak amplitude to this or above is an onset; 0 turns it off.",
            "0 or more");
  PARAM_DOC("onsetpitch", "1",
            "A pitch this many semitones from the current note's is an onset; 0 turns it off.",
            "0 or more semitones");
  PARAM_DOC("onsetperiod", "2048", "The fewest samples between two onsets.", "0 or more");
  PARAM_DOC("onsetlist", "0", "1 sends a list of the pitch and amplitude on an onset, 0 a bang.",
            "0 or 1");
  PARAM_DOC("tolerance", "0.15",
            "How far from perfectly periodic a signal may be and still have a pitch: lower is "
            "stricter.",
            "0 to 1");
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  float number = 0.f;
  if (!ReadWordAndNumber(message, begin, word, number)) return;
  if (Is(message, begin, word, "size")) {
    size = Samples(number);
  } else if (Is(message, begin, word, "period")) {
    period = Samples(number);
  } else if (Is(message, begin, word, "freqmin")) {
    freqmin = number;
  } else if (Is(message, begin, word, "freqmax")) {
    freqmax = number;
  } else if (Is(message, begin, word, "threshold")) {
    threshold = number;
  } else if (Is(message, begin, word, "onsetamp")) {
    onsetamp = number;
  } else if (Is(message, begin, word, "onsetpitch")) {
    onsetpitch = number;
  } else if (Is(message, begin, word, "onsetperiod")) {
    onsetperiod = Samples(number);
  } else if (Is(message, begin, word, "onsetlist")) {
    onsetlist = number != 0.f ? 1 : 0;
  } else if (Is(message, begin, word, "tolerance")) {
    tolerance = number;
  }
  // 'quiet' and anything else: nothing to do.
}

CALC() {
  (void)thread;
  fn.size(static_cast<UInt>(size.load() < 0 ? 0 : size.load()))
      .period(static_cast<UInt>(period.load() < 0 ? 0 : period.load()))
      .minFrequency(freqmin)
      .maxFrequency(freqmax)
      .threshold(threshold)
      .tolerance(tolerance)
      .onsetAmplitude(onsetamp)
      .onsetPitch(onsetpitch)
      .onsetPeriod(static_cast<UInt>(onsetperiod.load() < 0 ? 0 : onsetperiod.load()));

  // Unconnected, the inlet reads as silence (#1036).
  fn(inputs[0].Signal(0.f));

  // Never Send* from CALC(): the patcher calls FlushArmedMessages after
  // the traversal.
  if (fn.pitches() > 0) {
    sendFrequency = true;
    frequency = fn.frequency();
  }
  if (fn.reports() > 0) {
    sendAmplitude = true;
    amplitude = fn.amplitude();
  }
  if (fn.onsets() > 0) {
    sendOnset = true;
    onsetFrequency = fn.onsetFrequency();
    onsetAmplitude = fn.onsetPeak();
  }
  if (sendFrequency || sendAmplitude || sendOnset) MarkOutletArmed();
}

// After the block's traversal, on the audio thread: right outlet first.
void dFzero::FlushArmedMessages(YSE::THREAD thread) {
  if (sendOnset) {
    sendOnset = false;
    if (onsetlist.load() != 0) {
      scratch.clear();
      AppendNumber(scratch, onsetFrequency);
      scratch.push_back(' ');
      AppendNumber(scratch, onsetAmplitude);
      outputs[2].SendList(scratch, thread);
    } else {
      outputs[2].SendBang(thread);
    }
  }
  if (sendAmplitude) {
    sendAmplitude = false;
    outputs[1].SendFloat(amplitude, thread);
  }
  if (sendFrequency) {
    sendFrequency = false;
    outputs[0].SendFloat(frequency, thread);
  }
}

#undef className
