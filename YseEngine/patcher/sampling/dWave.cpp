#include "dWave.h"
#include <cmath>
#include <string_view>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t length = text.size();
    std::size_t begin = from;
    while (begin < length && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  // Up to `capacity` numbers from `from` on; a token that is not a number
  // (or is NaN) ends them. Returns how many were read.
  int Numbers(const std::string& text, std::size_t from, float* out, int capacity) {
    int count = 0;
    std::size_t next = from;
    while (count < capacity) {
      const std::string_view token = Token(text, next, next);
      float number = 0.f;
      if (token.empty() || !ReadNumericToken(token.data(), token.size(), number) ||
          std::isnan(number))
        break;
      out[count++] = number;
    }
    return count;
  }

  // A connected signal's first sample this block, else the inlet's number.
  float FirstSample(inlet& in, float number) {
    if (in.IsConstant()) return number;
    YSE::DSP::buffer& block = in.Signal(number);
    return block.getLength() > 0 ? block.getPtr()[0] : number;
  }

  // Max's interp: 0 to 6, a float truncated, NaN 1 (linear).
  int InterpMode(float value) {
    if (std::isnan(value)) return 1;
    if (value <= 0.f) return 0;
    return value >= 6.f ? 6 : static_cast<int>(value);
  }

  // A row count: truncated, at least 0, saturated at the int range.
  int RowCount(float value) {
    if (!(value >= 0.f)) return 0;
    if (!(value < 2147483648.f)) return 2147483647;
    return static_cast<int>(value);
  }

  int ClampChannels(int value) {
    const int most = static_cast<int>(YSE::DSP::waveReader::MAX_CHANNELS);
    return value < 1 ? 1 : (value > most ? most : value);
  }

  // `set [name] [start] [end]`: the numbers after the name are the region,
  // and without them the region is the whole buffer, as in Max.
  void SetRegionAfterName(const std::string& message, aFlt& start, aFlt& end) {
    std::size_t next = 0;
    Token(message, 0, next); // set
    Token(message, next, next); // the name
    float numbers[2] = {0.f, 0.f};
    Numbers(message, next, numbers, 2);
    start = numbers[0];
    end = numbers[1];
  }

  // A list `<start> [end]` in ms.
  void SetRegionFromList(const std::string& list, aFlt& start, aFlt& end) {
    float numbers[2] = {0.f, 0.f};
    const int count = Numbers(list, 0, numbers, 2);
    if (count >= 1) start = numbers[0];
    if (count >= 2) end = numbers[1];
  }

  const char* const PHASE_DOC_TAIL =
      " The region is one cycle: the phase wraps, so 1 reads where 0 does. A list of two "
      "numbers sets the start and end in milliseconds.";

} // namespace

// ---------------------------------------------------------------------------
// ~wave

#define className dWave

CONSTRUCT_DSP() {
  startMs = 0.f;
  endMs = 0.f;
  interpMode = static_cast<int>(DSP::waveReader::interpolation::LINEAR);
  bias = 0.f;
  tension = 0.f;
  constPhase = 0.f;

  // The outlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(startMs);
  ADD_PARAM(endMs);
  ADD_PARAM(channels);
  ADD_PARAM(interpMode);
  ADD_PARAM(bias);
  ADD_PARAM(tension);

  ADD_DESCRIPTION("Reads a region of a named sample buffer as one cycle of a waveform: a phase "
                  "from 0 to 1, such as a ~phasor, scans it from start to end, so any sample "
                  "becomes a wavetable oscillator.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The buffer read. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("start", "0", "The start of the region, in milliseconds into the buffer.",
            "0 or higher");
  PARAM_DOC("end", "0", "The end of the region, in milliseconds into the buffer; 0 is its end.",
            "0 or higher");
  PARAM_DOC("channels", "1",
            "The number of outlets, each reading the buffer channel of its number. Buffer "
            "channels past the last outlet are not read.",
            "1 to 64");
  PARAM_DOC("interp", "1",
            "How a position between two samples is read: 0 the sample before it, 1 and 2 a "
            "straight line, 3 a half cosine, 4 a cubic curve, 5 a Catmull-Rom spline, 6 a "
            "Hermite curve shaped by interp_bias and interp_tension.",
            "0 to 6");
  PARAM_DOC("interp_bias", "0",
            "The Hermite curve's bias (interp 6): 0 even, above 0 leaning to the sample before, "
            "below 0 to the sample after.",
            "any float");
  PARAM_DOC("interp_tension", "0",
            "The Hermite curve's tension (interp 6): 0 normal, 1 high (straight lines), -1 low "
            "(rounder).",
            "any float");
}

void dWave::ShapePorts() {
  // Rebuilt rather than resized, as ~groove: the outlets follow the
  // arguments, and the object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  // in 0: the phase (signal, float or int), the region list, the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);
  REG_LIST_IN(SetRegion);
  inputs.back().SetDoc("phase",
                       std::string("The phase, 0 to 1: a signal, or a number used while no "
                                   "signal is connected.") +
                           PHASE_DOC_TAIL +
                           " Also takes 'interp <0-6>', 'interp_bias <f>', 'interp_tension "
                           "<f>' and 'set <name> [start] [end]' (another buffer from a later "
                           "block, and its region or the whole buffer). None of them is saved.",
                       "0 to 1");

  // in 1, 2: the region, in ms
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetStart);
  REG_INT_IN(SetStartInt);
  inputs.back().SetDoc("start",
                       "The start of the region, in milliseconds into the buffer. A connected "
                       "signal sets it once per block, from the block's first sample.",
                       "0 or higher");
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetEnd);
  REG_INT_IN(SetEndInt);
  inputs.back().SetDoc("end",
                       "The end of the region, in milliseconds into the buffer; 0 is its end. A "
                       "connected signal sets it once per block, from the block's first sample.",
                       "0 or higher");

  for (int i = 0; i < channels; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channels == 1 ? "out" : "out " + n,
                          "Buffer channel " + n +
                              " read at the phase. Silence without a region, and for a channel "
                              "the buffer lacks.",
                          "any float");
  }
}

void dWave::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  channels = ClampChannels(channels);
  interpMode = InterpMode(static_cast<float>(interpMode.load()));
  reader.channels(static_cast<UInt>(channels)); // allocates: control thread
  ShapePorts();
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  constPhase = value;
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  constPhase = static_cast<float>(value);
}

LIST_IN(SetRegion) {
  (void)inlet;
  (void)thread;
  SetRegionFromList(value, startMs, endMs);
}

FLOAT_IN(SetStart) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) startMs = value;
}

INT_IN(SetStartInt) {
  (void)inlet;
  (void)thread;
  startMs = static_cast<float>(value);
}

FLOAT_IN(SetEnd) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) endMs = value;
}

INT_IN(SetEndInt) {
  (void)inlet;
  (void)thread;
  endMs = static_cast<float>(value);
}

// `set [name] [start] [end]`, `interp <n>`, `interp_bias <f>`,
// `interp_tension <f>`, on any thread.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) {
    SetRegionAfterName(message, startMs, endMs);
    return;
  }
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  if (Numbers(message, next, &number, 1) == 0) return;
  if (word == "interp") {
    interpMode = InterpMode(number);
  } else if (word == "interp_bias") {
    bias = number;
  } else if (word == "interp_tension") {
    tension = number;
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  // A connected signal on a region inlet sets that point once per block.
  reader.start(FirstSample(inputs[1], startMs.load()))
      .end(FirstSample(inputs[2], endMs.load()))
      .interp(static_cast<DSP::waveReader::interpolation>(interpMode.load()))
      .bias(bias.load())
      .tension(tension.load());

  // The phase: a connected signal, else the number (issue #1036).
  reader(view, inputs[0].Signal(constPhase));
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&reader.out(static_cast<UInt>(c)), thread);
}

#undef className

// ---------------------------------------------------------------------------
// ~2d.wave

#define className dWave2d

CONSTRUCT_DSP() {
  startMs = 0.f;
  endMs = 0.f;
  rows = 1;
  constPhase = 0.f;
  constRow = 0.f;

  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(startMs);
  ADD_PARAM(endMs);
  ADD_PARAM(channels);
  ADD_PARAM(rows);

  ADD_DESCRIPTION("Reads a region of a named sample buffer cut into rows, each one cycle of a "
                  "waveform: one phase from 0 to 1 scans a row, a second picks the row and "
                  "crossfades between two, so a slow ramp morphs from the first cycle to the "
                  "last (a two-dimensional wavetable).");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The buffer read. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("start", "0", "The start of the region, in milliseconds into the buffer.",
            "0 or higher");
  PARAM_DOC("end", "0", "The end of the region, in milliseconds into the buffer; 0 is its end.",
            "0 or higher");
  PARAM_DOC("channels", "1",
            "The number of outlets, each reading the buffer channel of its number. Buffer "
            "channels past the last outlet are not read.",
            "1 to 64");
  PARAM_DOC("rows", "1",
            "The number of rows the region is cut into, of equal length; 0 and 1 are one row.",
            "0 or higher");
}

void dWave2d::ShapePorts() {
  inputs.clear();
  outputs.clear();

  // in 0: the phase (signal, float or int), the region list, the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);
  REG_LIST_IN(SetRegion);
  inputs.back().SetDoc("phase",
                       std::string("The phase within a row, 0 to 1: a signal, or a number used "
                                   "while no signal is connected.") +
                           PHASE_DOC_TAIL +
                           " Also takes 'rows <n>' and 'set <name> [start] [end]' (another "
                           "buffer from a later block, and its region or the whole buffer). "
                           "None of them is saved.",
                       "0 to 1");

  // in 1: the row phase (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRow);
  REG_INT_IN(SetRowInt);
  inputs.back().SetDoc("row",
                       "Which row plays, 0 the first and 1 the last; between two rows both are "
                       "crossfaded. A signal, or a number used while no signal is connected. "
                       "Below 0 is 0, above 1 is 1.",
                       "0 to 1");

  // in 2, 3: the region, in ms
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetStart);
  REG_INT_IN(SetStartInt);
  inputs.back().SetDoc("start",
                       "The start of the region, in milliseconds into the buffer. A connected "
                       "signal sets it once per block, from the block's first sample.",
                       "0 or higher");
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetEnd);
  REG_INT_IN(SetEndInt);
  inputs.back().SetDoc("end",
                       "The end of the region, in milliseconds into the buffer; 0 is its end. A "
                       "connected signal sets it once per block, from the block's first sample.",
                       "0 or higher");

  for (int i = 0; i < channels; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channels == 1 ? "out" : "out " + n,
                          "Buffer channel " + n +
                              " read at the phase, in the row the row phase picks. Silence "
                              "without a region, and for a channel the buffer lacks.",
                          "any float");
  }
}

void dWave2d::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  EnableFileIO();
}

PARM_PARSE() {
  binding.Bind(bufferName);
  channels = ClampChannels(channels);
  rows = RowCount(static_cast<float>(rows.load()));
  reader.channels(static_cast<UInt>(channels)); // allocates: control thread
  ShapePorts();
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  constPhase = value;
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  constPhase = static_cast<float>(value);
}

LIST_IN(SetRegion) {
  (void)inlet;
  (void)thread;
  SetRegionFromList(value, startMs, endMs);
}

FLOAT_IN(SetRow) {
  (void)inlet;
  (void)thread;
  constRow = value;
}

INT_IN(SetRowInt) {
  (void)inlet;
  (void)thread;
  constRow = static_cast<float>(value);
}

FLOAT_IN(SetStart) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) startMs = value;
}

INT_IN(SetStartInt) {
  (void)inlet;
  (void)thread;
  startMs = static_cast<float>(value);
}

FLOAT_IN(SetEnd) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) endMs = value;
}

INT_IN(SetEndInt) {
  (void)inlet;
  (void)thread;
  endMs = static_cast<float>(value);
}

// `set [name] [start] [end]`, `rows <n>`, on any thread.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) {
    SetRegionAfterName(message, startMs, endMs);
    return;
  }
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  if (Numbers(message, next, &number, 1) == 0) return;
  if (word == "rows") rows = RowCount(number);
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  reader.start(FirstSample(inputs[2], startMs.load()))
      .end(FirstSample(inputs[3], endMs.load()))
      .rows(static_cast<UInt>(rows.load()));

  // The phases: connected signals, else the numbers (issue #1036).
  reader(view, inputs[0].Signal(constPhase), inputs[1].Signal(constRow));
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&reader.out(static_cast<UInt>(c)), thread);
}

#undef className
