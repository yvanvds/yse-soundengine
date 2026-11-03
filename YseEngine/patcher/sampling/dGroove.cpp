#include "dGroove.h"
#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_POSITION = 1;
  constexpr int REQUEST_STOP = 2;
  constexpr int REQUEST_START_LOOP = 3;
  constexpr int REQUEST_END_LOOP = 4;

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

  int LoopMode(float value) {
    if (!(value >= 1.f)) return 0; // also for NaN
    return value >= 2.f ? 2 : 1;
  }

} // namespace

#define className dGroove

CONSTRUCT_DSP() {
  loop = 0;
  loopInterp = 0;
  interpTime = DSP::groovePlayer::DEFAULT_INTERP_TIME;
  loopStart = 0.f;
  loopEnd = 0.f;
  requestPosition = 0.f;

  // The outlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channels);
  ADD_PARAM(loop);
  ADD_PARAM(loopInterp);
  ADD_PARAM(interpTime);
  ADD_PARAM(loopStart);
  ADD_PARAM(loopEnd);

  ADD_DESCRIPTION("Plays a named sample buffer at the speed of a signal, forward or backward, "
                  "between loop points it can loop or swing back and forth over, with a sync "
                  "signal that ramps from 0 to 1 over the loop: a sampler voice.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The buffer to play. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channels", "1",
            "The number of sample outlets, each playing the buffer channel of its number. Buffer "
            "channels past the last outlet are mixed into the outlets.",
            "1 to 64");
  PARAM_DOC("loop", "0",
            "0 plays through the loop points to an end of the buffer and stops there; 1 jumps "
            "back to the other loop point on reaching one; 2 turns round at each loop point.",
            "0, 1 or 2");
  PARAM_DOC("loopinterp", "0",
            "1 crossfades a loop (loop 1) across its jump, so it does not click.", "0 or 1");
  PARAM_DOC("interptime", "50",
            "The length of the loop crossfade, in milliseconds; at most the length of the loop.",
            "0 or higher");
  PARAM_DOC("loopstart", "0", "The loop start, in milliseconds into the buffer.", "0 or higher");
  PARAM_DOC("loopend", "0", "The loop end, in milliseconds into the buffer; 0 is its end.",
            "0 or higher");
}

void dGroove::ShapePorts() {
  // Rebuilt rather than resized, as ~play: the outlets follow the
  // arguments, and the object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  // in 0: the speed signal, the position, and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPosition);
  REG_INT_IN(SetPositionInt);
  INLET_DOC(0, "speed",
            "The speed to play at, a signal: 1 is normal speed, 2 twice as fast, -1 backward, 0 "
            "holds the sample at the position. Unconnected, the speed is 0. A number jumps to "
            "that position, in milliseconds into the buffer, and plays from there, also after "
            "a stop. Also takes 'setloop <start> <end>', 'startloop' and 'endloop' (jump to a "
            "loop point and play), 'stop', 'reset' (the loop is the whole buffer), 'loop "
            "<0/1/2>', 'loopinterp <0/1>', 'interptime <ms>', 'loopstart <ms>', 'loopend <ms>' "
            "and 'set <name>' (another buffer, from a later block). None of them is saved.",
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
                              " as it plays. Silence once stopped, and for a "
                              "channel the buffer lacks.",
                          "any float");
  }
  ADD_OUT_BUFFER;
  outputs.back().SetDoc("sync",
                        "Where in the loop each sample is read: 0 at the loop start rising to 1 "
                        "at the loop end. 0 outside the loop and while stopped.",
                        "0 to 1");
}

void dGroove::SetParent(pObject* newParent) {
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
  loop = LoopMode(static_cast<float>(loop.load()));
  loopInterp = loopInterp.load() != 0 ? 1 : 0;
  const float fade = interpTime.load();
  interpTime = fade > 0.f ? fade : 0.f;
  player.channels(static_cast<UInt>(channels)); // allocates: control thread
  ShapePorts();
}

void dGroove::Post(int what, float positionMs) {
  // The position first: a block that sees the request sees it.
  requestPosition.store(positionMs, std::memory_order_relaxed);
  request.store(what, std::memory_order_release);
}

FLOAT_IN(SetPosition) {
  (void)inlet;
  (void)thread;
  if (std::isnan(value)) return;
  Post(REQUEST_POSITION, value);
}

INT_IN(SetPositionInt) {
  (void)inlet;
  (void)thread;
  Post(REQUEST_POSITION, static_cast<float>(value));
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

  if (Is(text, begin, end, "stop")) {
    Post(REQUEST_STOP);
  } else if (Is(text, begin, end, "startloop")) {
    Post(REQUEST_START_LOOP);
  } else if (Is(text, begin, end, "endloop")) {
    Post(REQUEST_END_LOOP);
  } else if (Is(text, begin, end, "reset")) {
    loopStart = 0.f;
    loopEnd = 0.f;
  } else if (Is(text, begin, end, "setloop")) {
    if (count < 2) return;
    loopStart = numbers[0];
    loopEnd = numbers[1];
  } else if (count == 0) {
    return; // the attributes need their number
  } else if (Is(text, begin, end, "loop")) {
    loop = LoopMode(numbers[0]);
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

  switch (request.exchange(REQUEST_NONE, std::memory_order_acquire)) {
  case REQUEST_POSITION:
    player.position(requestPosition.load(std::memory_order_relaxed));
    break;
  case REQUEST_STOP:
    player.stop();
    break;
  case REQUEST_START_LOOP:
    player.startLoop();
    break;
  case REQUEST_END_LOOP:
    player.endLoop();
    break;
  default:
    break;
  }

  // A connected signal on a loop inlet sets that point once per block.
  const float start = FirstSample(inputs[1], loopStart.load());
  const float finish = FirstSample(inputs[2], loopEnd.load());
  const int mode = loop.load();
  player
      .loop(mode == 2   ? DSP::GROOVE_LOOP::PENDULUM
            : mode == 1 ? DSP::GROOVE_LOOP::ON
                        : DSP::GROOVE_LOOP::OFF)
      .loopInterp(loopInterp.load() != 0)
      .interpTime(interpTime)
      .loopPoints(start, finish);

  // The speed: a connected signal, else 0 (the numbers are positions).
  player(view, inputs[0].Signal(0.f));
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&player.out(static_cast<UInt>(c)), thread);
  outputs[channels].SendBuffer(&player.sync(), thread);
}

#undef className
