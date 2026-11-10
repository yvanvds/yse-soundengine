#include "dVectral.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::vectral;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && !std::isnan(out);
  }

  // A frame size as an int the DSP class clamps further to its capacity.
  int Frames(float value) {
    if (!(value >= 1.f)) return 1;
    const auto most = static_cast<float>(vectral::MAXIMUM_SIZE);
    return value >= most ? static_cast<int>(vectral::MAXIMUM_SIZE) : static_cast<int>(value);
  }
} // namespace

#define className dVectral

CONSTRUCT_DSP() {
  // in 0: the output index; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the input index; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 2: the frame values; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the smoothed frames
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(size);

  ADD_DESCRIPTION("Smooths a stream of frames, such as spectra, index by index: each value "
                  "against the value at the same index in the frames before it, with a linear "
                  "ramp (rampsmooth), a logarithmic slide or a limited change per frame "
                  "(deltaclip).");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "outIndex",
            "The output index: which stored value each output sample reads, usually a frame's "
            "bin index. A signal; unconnected, the last number sent here (0). Also takes "
            "'rampsmooth <up> [down]', 'slide <up> [down]', 'deltaclip <lowest> <highest>', "
            "'size <n>' and 'clear'.",
            "0 to size - 1");
  INLET_DOC(1, "inIndex",
            "The input index: where each input sample is smoothed and stored. Equal to the "
            "output index, frames come out smoothed in the frame they arrive in; ahead of it "
            "by d, shifted by d places. A signal; unconnected, the last number sent here (0). "
            "Takes the same words as the left inlet.",
            "0 to size - 1");
  INLET_DOC(2, "in",
            "The frame values to smooth, one per sample. A signal; unconnected, the last number "
            "sent here (0). Takes the same words as the left inlet.",
            "");
  OUTLET_DOC(0, "out",
             "The stored value at the output index, after this sample's input was smoothed in; "
             "0 when the index is outside the frame.",
             "");
  PARAM_DOC("size", "512",
            "The frame size: indexes 0 to size - 1 are used. Also the largest size the 'size' "
            "message can set. Changing it rebuilds the object.",
            "1-65536");
}

// Control thread: the clear and parse callbacks make a live change rebuild
// the object, so the frame is allocated on one no audio thread has seen.
// Clear runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  size = static_cast<int>(vectral::DEFAULT_SIZE);
  if (fn.capacity() != vectral::DEFAULT_SIZE) fn = vectral();
  frameSize = size;
}

PARM_PARSE() {
  size = Frames(static_cast<float>(size));
  if (static_cast<UInt>(size) != fn.capacity()) fn = vectral(static_cast<UInt>(size));
  frameSize = size;
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet >= 0 && inlet < 3) numbers[inlet] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearPending = true;
    return;
  }
  float first = 0.f;
  float second = 0.f;
  const bool haveFirst = ReadNumber(Token(message, next, next), first);
  const bool haveSecond = haveFirst && ReadNumber(Token(message, next, next), second);
  if (!haveFirst) return;
  // The settings first, the mode last: a block that sees the new mode sees
  // its settings.
  if (word == "rampsmooth") {
    rampUp = first;
    rampDown = haveSecond ? second : first;
    mode = vectral::RAMPSMOOTH;
  } else if (word == "slide") {
    slideUp = first;
    slideDown = haveSecond ? second : first;
    mode = vectral::SLIDE;
  } else if (word == "deltaclip") {
    if (!haveSecond) return;
    clipLowest = first;
    clipHighest = second;
    mode = vectral::DELTACLIP;
  } else if (word == "size") {
    frameSize = Frames(first);
  }
}

CALC() {
  switch (mode.load()) {
  case vectral::SLIDE:
    fn.slide(slideUp.load(), slideDown.load());
    break;
  case vectral::DELTACLIP:
    fn.deltaclip(clipLowest.load(), clipHighest.load());
    break;
  default:
    fn.rampsmooth(rampUp.load(), rampDown.load());
    break;
  }
  fn.size(static_cast<UInt>(frameSize.load()));
  if (clearPending.exchange(false)) fn.clear();

  // Unconnected inlets read their numbers (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(numbers[0].load()), inputs[1].Signal(numbers[1].load()),
                        inputs[2].Signal(numbers[2].load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
