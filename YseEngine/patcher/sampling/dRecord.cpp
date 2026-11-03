#include "dRecord.h"
#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The record requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_START = 1;
  constexpr int REQUEST_STOP = 2;

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

} // namespace

#define className dRecord

CONSTRUCT_DSP() {
  append = 0;
  loop = 0;
  loopStart = 0.f;
  loopEnd = 0.f;

  // The inlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  // out 0: the sync signal
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channels);
  ADD_PARAM(append);
  ADD_PARAM(loop);
  ADD_PARAM(loopStart);
  ADD_PARAM(loopEnd);

  ADD_DESCRIPTION("Records signals into a named sample buffer, between a start and an end point, "
                  "once or in a loop, with a sync signal that ramps from 0 to 1 over the "
                  "recording: live looping and sampling.");
  ADD_CATEGORY(pCategory::SAMPLING);
  OUTLET_DOC(0, "sync",
             "While recording, where each sample goes: 0 at the start point rising to 1 at the "
             "end point. 0 while not recording.",
             "0 to 1");
  PARAM_DOC("name", "", "The buffer to record into. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channels", "1",
            "The number of signal inlets, each recorded into the buffer channel of its number.",
            "1 to 64");
  PARAM_DOC("append", "0",
            "1 makes a start carry on where the last recording stopped, instead of at the start "
            "point.",
            "0 or 1");
  PARAM_DOC("loop", "0",
            "1 makes recording carry on from the start point when it reaches the end point, "
            "instead of stopping.",
            "0 or 1");
  PARAM_DOC("loopstart", "0", "The start point, in milliseconds into the buffer.", "0 or higher");
  PARAM_DOC("loopend", "0", "The end point, in milliseconds into the buffer; 0 is its end.",
            "0 or higher");
}

void dRecord::ShapePorts() {
  // Rebuilt rather than resized, as ~loudness: the inlets follow the
  // arguments, and the object is new, so no cord ends on them yet.
  inputs.clear();
  for (int i = 0; i < channels; i++) {
    if (i == 0) {
      // in 0: signal 1, the record switch, and the word messages
      ADD_IN_0;
      REG_SIGNAL_IN;
      REG_FLOAT_IN(SetRecord);
      REG_INT_IN(SetRecordInt);
      INLET_DOC(0, channels == 1 ? "signal" : "signal 1",
                "The signal to record into channel 1; unconnected it records silence. A number "
                "other than 0 starts recording, 0 stops it. Also takes 'append <0/1>', 'loop "
                "<0/1>', 'loopstart <ms>', 'loopend <ms>', 'reset' (the whole buffer again) and "
                "'set <name>' (another buffer, from a later block). None of them is saved.",
                "any float");
    } else {
      inputs.emplace_back(this, false, i);
      REG_SIGNAL_IN;
      const std::string n = std::to_string(i + 1);
      inputs.back().SetDoc("signal " + n,
                           "The signal to record into channel " + n +
                               "; unconnected it records silence.",
                           "any float");
    }
  }
  // the start and end points, in ms
  inputs.emplace_back(this, false, channels);
  REG_FLOAT_IN(SetStart);
  REG_INT_IN(SetStartInt);
  inputs.back().SetDoc("start", "The start point, in milliseconds into the buffer.", "0 or higher");
  inputs.emplace_back(this, false, channels + 1);
  REG_FLOAT_IN(SetEnd);
  REG_INT_IN(SetEndInt);
  inputs.back().SetDoc("end", "The end point, in milliseconds into the buffer; 0 is its end.",
                       "0 or higher");
}

void dRecord::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  const int most = static_cast<int>(DSP::bufferRecorder::MAX_CHANNELS);
  channels = channels < 1 ? 1 : (channels > most ? most : channels);
  append = append.load() != 0 ? 1 : 0;
  loop = loop.load() != 0 ? 1 : 0;
  ShapePorts();
}

void dRecord::PostRecord(float value) {
  if (std::isnan(value)) return;
  request.store(value != 0.f ? REQUEST_START : REQUEST_STOP, std::memory_order_relaxed);
}

FLOAT_IN(SetRecord) {
  (void)inlet;
  (void)thread;
  PostRecord(value);
}

INT_IN(SetRecordInt) {
  (void)inlet;
  (void)thread;
  PostRecord(static_cast<float>(value));
}

FLOAT_IN(SetStart) {
  (void)inlet;
  (void)thread;
  loopStart = value;
}

INT_IN(SetStartInt) {
  (void)inlet;
  (void)thread;
  loopStart = static_cast<float>(value);
}

FLOAT_IN(SetEnd) {
  (void)inlet;
  (void)thread;
  loopEnd = value;
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
  if (Is(text, begin, end, "reset")) {
    loopStart = 0.f;
    loopEnd = 0.f;
    return;
  }

  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  float number = 0.f;
  if (!ReadNumericToken(text + argBegin, argEnd - argBegin, number) || std::isnan(number)) return;
  if (Is(text, begin, end, "append")) {
    append = number != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "loop")) {
    loop = number != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "loopstart")) {
    loopStart = number;
  } else if (Is(text, begin, end, "loopend")) {
    loopEnd = number;
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleWriteView view = binding.Current().pin();

  const int pending = request.exchange(REQUEST_NONE, std::memory_order_relaxed);
  if (pending == REQUEST_START) {
    recorder.record(true);
  } else if (pending == REQUEST_STOP) {
    recorder.record(false);
  }
  recorder.append(append.load() != 0).loop(loop.load() != 0).loopPoints(loopStart, loopEnd);

  // Every signal inlet, connected or not: an unconnected one records 0.
  const auto count = static_cast<UInt>(channels);
  for (UInt i = 0; i < count; ++i)
    blocks[i] = &inputs[i].Signal(0.f);
  DSP::buffer& out = recorder(view, blocks.data(), count);
  outputs[0].SendBuffer(&out, thread);
}

#undef className
