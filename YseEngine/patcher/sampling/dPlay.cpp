#include "dPlay.h"
#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The transport requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_START = 1;
  constexpr int REQUEST_STOP = 2;
  constexpr int REQUEST_PAUSE = 3;
  constexpr int REQUEST_RESUME = 4;

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

#define className dPlay

CONSTRUCT_DSP() {
  loop = 0;
  loopInterp = 0;
  interpTime = DSP::bufferPlayer::DEFAULT_INTERP_TIME;
  requestStart = 0.f;
  requestEnd = -1.f;
  requestDuration = 0.f;

  // The outlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channels);
  ADD_PARAM(loop);
  ADD_PARAM(loopInterp);
  ADD_PARAM(interpTime);

  ADD_DESCRIPTION("Plays a named sample buffer from a start to an end point at normal speed or "
                  "over a given time, forward or backward, once or in a loop, or at the "
                  "positions a signal gives; bangs when playback reaches its end.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The buffer to play. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channels", "1",
            "The number of signal outlets, each playing the buffer channel of its number. Buffer "
            "channels past the last outlet are mixed into the outlets.",
            "1 to 64");
  PARAM_DOC("loop", "0",
            "1 makes playback carry on from the start point when it reaches the end point, "
            "instead of stopping.",
            "0 or 1");
  PARAM_DOC("loopinterp", "0",
            "1 crossfades a loop across its jump back to the start point, so it does not click.",
            "0 or 1");
  PARAM_DOC("interptime", "50",
            "The length of the loop crossfade, in milliseconds; at most the length of the loop.",
            "0 or higher");
}

void dPlay::ShapePorts() {
  // Rebuilt rather than resized, as ~sig: the outlets follow the arguments,
  // and the object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  // in 0: the position signal, the switch, and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPlay);
  REG_INT_IN(SetPlayInt);
  INLET_DOC(0, "position",
            "A connected signal is the position to play, in milliseconds into the buffer: rising "
            "plays forward, falling backward. Unconnected, a number other than 0 plays the whole "
            "buffer and 0 stops. Also takes 'start [start] [end] [duration]' (in ms: from start "
            "to end, backward when end comes first, over duration or at normal speed), 'stop', "
            "'pause', 'resume', 'loop <0/1>', 'loopinterp <0/1>', 'interptime <ms>' and 'set "
            "<name>' (another buffer, from a later block). None of them is saved.",
            "0 or higher");

  for (int i = 0; i < channels; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channels == 1 ? "out" : "out " + n,
                          "Buffer channel " + n +
                              " as it plays. Silence while stopped or "
                              "paused, and for a channel the buffer lacks.",
                          "any float");
  }
  ADD_OUT_BANG;
  outputs.back().SetDoc("done",
                        "A bang after the block in which playback reached its end point. Not "
                        "sent in a loop, on stop, or while following a position signal.",
                        "bang");
}

void dPlay::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  const int most = static_cast<int>(DSP::bufferPlayer::MAX_CHANNELS);
  channels = channels < 1 ? 1 : (channels > most ? most : channels);
  loop = loop.load() != 0 ? 1 : 0;
  loopInterp = loopInterp.load() != 0 ? 1 : 0;
  const float fade = interpTime.load();
  interpTime = fade > 0.f ? fade : 0.f;
  player.channels(static_cast<UInt>(channels)); // allocates: control thread
  ShapePorts();
}

void dPlay::Post(int what, float startMs, float endMs, float durationMs) {
  // The numbers first: a block that sees the request sees them.
  requestStart.store(startMs, std::memory_order_relaxed);
  requestEnd.store(endMs, std::memory_order_relaxed);
  requestDuration.store(durationMs, std::memory_order_relaxed);
  request.store(what, std::memory_order_release);
}

FLOAT_IN(SetPlay) {
  (void)inlet;
  (void)thread;
  if (std::isnan(value)) return;
  Post(value != 0.f ? REQUEST_START : REQUEST_STOP);
}

INT_IN(SetPlayInt) {
  (void)inlet;
  (void)thread;
  Post(value != 0 ? REQUEST_START : REQUEST_STOP);
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

  // Up to three numbers after the word. A word that is not a number ends
  // them.
  float numbers[3] = {0.f, -1.f, 0.f};
  int count = 0;
  std::size_t pos = end;
  while (count < 3) {
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

  if (Is(text, begin, end, "start")) {
    Post(REQUEST_START, numbers[0], numbers[1], numbers[2]);
  } else if (Is(text, begin, end, "stop")) {
    Post(REQUEST_STOP);
  } else if (Is(text, begin, end, "pause")) {
    Post(REQUEST_PAUSE);
  } else if (Is(text, begin, end, "resume")) {
    Post(REQUEST_RESUME);
  } else if (count == 0) {
    return; // the attributes need their number
  } else if (Is(text, begin, end, "loop")) {
    loop = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "loopinterp")) {
    loopInterp = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "interptime")) {
    interpTime = numbers[0] > 0.f ? numbers[0] : 0.f;
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  switch (request.exchange(REQUEST_NONE, std::memory_order_acquire)) {
  case REQUEST_START:
    player.start(requestStart.load(std::memory_order_relaxed),
                 requestEnd.load(std::memory_order_relaxed),
                 requestDuration.load(std::memory_order_relaxed));
    break;
  case REQUEST_STOP:
    player.stop();
    break;
  case REQUEST_PAUSE:
    player.pause();
    break;
  case REQUEST_RESUME:
    player.resume();
    break;
  default:
    break;
  }
  player.loop(loop.load() != 0).loopInterp(loopInterp.load() != 0).interpTime(interpTime);

  // A connected signal is the position; unconnected, the player plays on its
  // own (its numbers are the switch, not a position).
  if (inputs[0].IsConstant()) {
    player(view);
  } else {
    player(view, inputs[0].Signal(0.f));
  }
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&player.out(static_cast<UInt>(c)), thread);
  if (player.done()) outputs[channels].ArmBang();
}

#undef className
