#include "dBuffir.h"
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

  // The number in the token after `from`; false when there is none, it is
  // not a number, or it is NaN. `next` is set to where the token ends.
  bool NumberAfter(const std::string& text, std::size_t from, std::size_t& next, float& number) {
    const std::string_view token = Token(text, from, next);
    return !token.empty() && ReadNumericToken(token.data(), token.size(), number) &&
           !std::isnan(number);
  }

  // A frame count or channel from a number: truncated toward 0, at least
  // `lowest`, saturated at the int range; NaN is `lowest`.
  int Whole(float value, int lowest) {
    if (!(value >= static_cast<float>(lowest))) return lowest;
    if (!(value < 2147483648.f)) return 2147483647;
    return static_cast<int>(value);
  }

  int AtLeast(int value, int lowest) {
    return value < lowest ? lowest : value;
  }

  // A connected signal's first sample this block, as a frame count, else
  // the inlet's number.
  int FirstFrames(inlet& in, int number) {
    if (in.IsConstant()) return number;
    YSE::DSP::buffer& block = in.Signal(0.f);
    return block.getLength() > 0 ? Whole(block.getPtr()[0], 0) : number;
  }

} // namespace

#define className dBuffir

CONSTRUCT_DSP() {
  constInput = 0.f;

  // in 0: the input (signal, float or int), and the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);

  // in 1: the offset (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetOffsetFloat);
  REG_INT_IN(SetOffset);

  // in 2: the size (signal, float or int)
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSizeFloat);
  REG_INT_IN(SetSize);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(tapOffset);
  ADD_PARAM(tapSize);
  ADD_PARAM(channel);

  ADD_DESCRIPTION("A FIR filter whose taps are part of a named sample buffer: the signal "
                  "convolved with up to 4096 of its samples, for custom filters and short impulse "
                  "responses.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "input",
            "The signal filtered, or a number used while no signal is connected. 'clear' sets "
            "the filter's input history to 0. 'chan <n>', 'offset <n>' and 'size <n>' change "
            "the channel and the taps; 'set <name> [offset] [size]' switches to another buffer "
            "from a later block (none of them saved with the patch).",
            "any float");
  INLET_DOC(1, "offset",
            "The frame of the first tap: a number, or a signal read at the first sample of each "
            "block. A number is not saved with the patch.",
            "0 or higher");
  INLET_DOC(2, "size",
            "The number of taps, at most 4096: a number, or a signal read at the first sample "
            "of each block. A number is not saved with the patch.",
            "0 to 4096");
  OUTLET_DOC(0, "out",
             "The input convolved with the taps: an impulse gives the taps back in buffer order. "
             "Silence without a buffer, or when there are no taps.",
             "any float");
  PARAM_DOC("name", "",
            "The buffer the taps are read from. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("offset", "0", "The frame of the first tap.", "0 or higher");
  PARAM_DOC("size", "0",
            "The number of taps, at most 4096; past the end of the buffer they stop at its "
            "last frame. 0 is silence.",
            "0 to 4096");
  PARAM_DOC("chan", "1", "The buffer channel read, from 1.", "1 or higher");
}

void dBuffir::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  tapOffset.store(AtLeast(tapOffset.load(std::memory_order_relaxed), 0), std::memory_order_relaxed);
  tapSize.store(AtLeast(tapSize.load(std::memory_order_relaxed), 0), std::memory_order_relaxed);
  channel.store(AtLeast(channel.load(std::memory_order_relaxed), 1), std::memory_order_relaxed);
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  constInput = value;
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  constInput = static_cast<float>(value);
}

FLOAT_IN(SetOffsetFloat) {
  (void)inlet;
  (void)thread;
  tapOffset.store(Whole(value, 0), std::memory_order_relaxed);
}

INT_IN(SetOffset) {
  (void)inlet;
  (void)thread;
  tapOffset.store(AtLeast(value, 0), std::memory_order_relaxed);
}

FLOAT_IN(SetSizeFloat) {
  (void)inlet;
  (void)thread;
  tapSize.store(Whole(value, 0), std::memory_order_relaxed);
}

INT_IN(SetSize) {
  (void)inlet;
  (void)thread;
  tapSize.store(AtLeast(value, 0), std::memory_order_relaxed);
}

// `set [name] [offset] [size]`, `clear`, `chan <n>`, `offset <n>`,
// `size <n>`, on any thread.
MESSAGES() {
  (void)value;
  std::size_t next = 0;
  float number = 0.f;
  if (binding.HandleSet(*this, message)) {
    Token(message, 0, next); // set
    Token(message, next, next); // the name
    if (!NumberAfter(message, next, next, number)) return;
    tapOffset.store(Whole(number, 0), std::memory_order_relaxed);
    if (!NumberAfter(message, next, next, number)) return;
    tapSize.store(Whole(number, 0), std::memory_order_relaxed);
    return;
  }
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearPending.store(true, std::memory_order_relaxed);
    return;
  }
  if (!NumberAfter(message, next, next, number)) return;
  if (word == "chan") {
    channel.store(Whole(number, 1), std::memory_order_relaxed);
  } else if (word == "offset") {
    tapOffset.store(Whole(number, 0), std::memory_order_relaxed);
  } else if (word == "size") {
    tapSize.store(Whole(number, 0), std::memory_order_relaxed);
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();
  if (clearPending.exchange(false, std::memory_order_relaxed)) fir.clear();

  // The offset and size: a connected signal's first sample wins over the
  // number (issue #1036), read once per block as the taps are.
  fir.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1))
      .offset(FirstFrames(inputs[1], tapOffset.load(std::memory_order_relaxed)))
      .size(FirstFrames(inputs[2], tapSize.load(std::memory_order_relaxed)));

  DSP::buffer& out = inputs[0].IsConstant() ? fir(view, constInput.load())
                                            : fir(view, inputs[0].Signal(constInput));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
