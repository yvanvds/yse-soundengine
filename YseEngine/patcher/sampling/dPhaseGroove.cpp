#include "dPhaseGroove.h"
#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // One word of `text` from `pos`: [begin, end). Empty at the end. Walked in
  // place: no allocation on whichever thread sends.
  void NextToken(const char* text, std::size_t length, std::size_t pos, std::size_t& begin,
                 std::size_t& end) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    begin = pos;
    while (pos < length && !IsSelectorSeparator(text[pos]))
      pos++;
    end = pos;
  }

  bool Is(const char* text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::strlen(word);
    return end - begin == length && std::strncmp(text + begin, word, length) == 0;
  }

  // A connected signal's first sample this block, else the inlet's number.
  float FirstSample(inlet& in, float number) {
    if (in.IsConstant()) return number;
    YSE::DSP::buffer& block = in.Signal(number);
    return block.getLength() > 0 ? block.getPtr()[0] : number;
  }

} // namespace

#define className dPhaseGroove

CONSTRUCT_DSP() {
  loopInterp = 0;
  interpTime = DSP::groovePlayer::DEFAULT_INTERP_TIME;
  loopStart = 0.f;
  loopEnd = 0.f;
  constPhase = 0.f;

  // The outlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channels);
  ADD_PARAM(loopInterp);
  ADD_PARAM(interpTime);
  ADD_PARAM(loopStart);
  ADD_PARAM(loopEnd);

  ADD_DESCRIPTION("Plays the loop of a named sample buffer where a ramp from 0 to 1 says, so a "
                  "~phasor plays it once per cycle: tempo-locked loops and slicing.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The buffer to play. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channels", "1",
            "The number of sample outlets, each playing the buffer channel of its number. Buffer "
            "channels past the last outlet are mixed into the outlets.",
            "1 to 64");
  PARAM_DOC("loopinterp", "0",
            "1 crossfades the loop across the point the ramp wraps at, so it does not click.",
            "0 or 1");
  PARAM_DOC("interptime", "50",
            "The length of the loop crossfade, in milliseconds; at most the length of the loop.",
            "0 or higher");
  PARAM_DOC("loopstart", "0", "The loop start, in milliseconds into the buffer.", "0 or higher");
  PARAM_DOC("loopend", "0", "The loop end, in milliseconds into the buffer; 0 is its end.",
            "0 or higher");
}

void dPhaseGroove::ShapePorts() {
  // Rebuilt rather than resized, as ~groove: the outlets follow the
  // arguments, and the object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  // in 0: the phase signal, a constant phase, and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);
  INLET_DOC(0, "phase",
            "Where in the loop to read, a signal: 0 is the loop start, rising to 1 at the loop "
            "end, and a phase outside that wraps round the loop. Unconnected, it is the last "
            "number sent, else 0. Also takes 'setloop <start> <end>', 'reset' (the loop is the "
            "whole buffer), 'loopinterp <0/1>', 'interptime <ms>', 'loopstart <ms>', 'loopend "
            "<ms>' and 'set <name>' (another buffer, from a later block). None of them is saved.",
            "any float");

  // in 1, 2: the loop points, in ms
  inputs.emplace_back(this, false, 1);
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetStart);
  REG_INT_IN(SetStartInt);
  inputs.back().SetDoc("loop start",
                       "The loop start, in milliseconds into the buffer. A connected signal sets "
                       "it once per block, from the block's first sample.",
                       "0 or higher");
  inputs.emplace_back(this, false, 2);
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetEnd);
  REG_INT_IN(SetEndInt);
  inputs.back().SetDoc("loop end",
                       "The loop end, in milliseconds into the buffer; 0 is its end. A connected "
                       "signal sets it once per block, from the block's first sample.",
                       "0 or higher");

  for (int i = 0; i < channels; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channels == 1 ? "out" : "out " + n,
                          "Buffer channel " + n +
                              " at the phase. Silence for a channel the buffer lacks and for a "
                              "loop that holds nothing.",
                          "any float");
  }
  ADD_OUT_BUFFER;
  outputs.back().SetDoc("sync",
                        "The phase each sample is read at, kept in 0 to 1. 0 with no buffer and "
                        "for a loop that holds nothing.",
                        "0 to 1");
}

void dPhaseGroove::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  const int most = static_cast<int>(DSP::groovePlayer::MAX_CHANNELS);
  channels = channels < 1 ? 1 : (channels > most ? most : channels);
  loopInterp = loopInterp.load() != 0 ? 1 : 0;
  const float fade = interpTime.load();
  interpTime = fade > 0.f ? fade : 0.f;
  player.channels(static_cast<UInt>(channels)); // allocates: control thread
  ShapePorts();
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) constPhase = value;
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  constPhase = static_cast<float>(value);
}

FLOAT_IN(SetStart) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) loopStart = value;
}

INT_IN(SetStartInt) {
  (void)inlet;
  (void)thread;
  loopStart = static_cast<float>(value);
}

FLOAT_IN(SetEnd) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) loopEnd = value;
}

INT_IN(SetEndInt) {
  (void)inlet;
  (void)thread;
  loopEnd = static_cast<float>(value);
}

// The word messages, on any thread. Read in place: nothing allocates.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) return;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  if (begin == end) return;

  // Up to two numbers after the word. A word that is not a number ends
  // them.
  float numbers[2] = {0.f, 0.f};
  int count = 0;
  std::size_t pos = end;
  while (count < 2) {
    std::size_t argBegin = 0;
    std::size_t argEnd = 0;
    NextToken(text, length, pos, argBegin, argEnd);
    float number = 0.f;
    if (argBegin == argEnd || !ReadNumericToken(text + argBegin, argEnd - argBegin, number) ||
        std::isnan(number))
      break;
    numbers[count++] = number;
    pos = argEnd;
  }

  if (Is(text, begin, end, "reset")) {
    loopStart = 0.f;
    loopEnd = 0.f;
  } else if (Is(text, begin, end, "setloop")) {
    if (count < 2) return;
    loopStart = numbers[0];
    loopEnd = numbers[1];
  } else if (count == 0) {
    return; // the attributes need their number
  } else if (Is(text, begin, end, "loopinterp")) {
    loopInterp = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "interptime")) {
    interpTime = numbers[0] > 0.f ? numbers[0] : 0.f;
  } else if (Is(text, begin, end, "loopstart")) {
    loopStart = numbers[0];
  } else if (Is(text, begin, end, "loopend")) {
    loopEnd = numbers[0];
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  // A connected signal on a loop inlet sets that point once per block.
  const float start = FirstSample(inputs[1], loopStart.load());
  const float finish = FirstSample(inputs[2], loopEnd.load());
  player.loopInterp(loopInterp.load() != 0).interpTime(interpTime).loopPoints(start, finish);

  player.phase(view, inputs[0].Signal(constPhase));
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&player.out(static_cast<UInt>(c)), thread);
  outputs[channels].SendBuffer(&player.sync(), thread);
}

#undef className
