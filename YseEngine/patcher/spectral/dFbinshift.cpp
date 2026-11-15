#include "dFbinshift.h"

#include <string_view>
#include "../pListArgs.h"
#include "../pSelector.h"
#include "spectralPass.h"

using namespace YSE::PATCHER;
using YSE::DSP::spectralFreqShift;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const char* text, std::size_t size, std::size_t from, std::size_t& next) {
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(text[end]))
      end++;
    next = end;
    return std::string_view(text + begin, end - begin);
  }

  // A parameter held between `lowest` and `highest`.
  UInt Between(int value, UInt lowest, UInt highest) {
    if (value < static_cast<int>(lowest)) return lowest;
    const auto v = static_cast<UInt>(value);
    return v > highest ? highest : v;
  }

  // `from` as `to`'s contents, within the capacity `to` was built with.
  void Copy(YSE::DSP::buffer& from, YSE::DSP::buffer& to) {
    const UInt length = std::min(from.getLength(), YSE::STANDARD_BUFFERSIZE);
    if (to.getLength() != length) to.resize(length);
    std::copy(from.getPtr(), from.getPtr() + length, to.getPtr());
  }
} // namespace

#define className dFbinshift

CONSTRUCT_DSP() {
  // in 0: the real parts; clip
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the imaginary parts
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 2: the frequency shift in Hz
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0, 1: the shifted frame
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(shift);
  ADD_PARAM(bins);

  ADD_DESCRIPTION("A frequency shifter for a ~pfft's spectral frames: every bin of the spectrum "
                  "moved by the same number of Hz, the frame's phase turned so the moved "
                  "partials stay continuous from frame to frame.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "real",
            "The real parts of the frames, from ~fftin's outlet 0. A signal; unconnected, the "
            "last number sent here (0). 'clip 1' drops the bins moved below 0 Hz or above "
            "Nyquist, 'clip 0' (the default) folds them back as they would alias. Not saved.",
            "clip");
  INLET_DOC(1, "imaginary",
            "The imaginary parts of the frames, from ~fftin's outlet 1. A signal; unconnected, "
            "the last number sent here (0).",
            "");
  INLET_DOC(2, "shift",
            "The frequency shift in Hz, either sign: added to every frequency. A signal, read "
            "at the first bin of each frame, or a number while unconnected (the shift "
            "argument).",
            "Hz");
  OUTLET_DOC(0, "real",
             "The real parts of the shifted frames, for ~fftout. Outside a ~pfft, the real input "
             "unchanged.",
             "");
  OUTLET_DOC(1, "imaginary",
             "The imaginary parts of the shifted frames, for ~fftout. Outside a ~pfft, the "
             "imaginary input unchanged.",
             "");
  PARAM_DOC("shift", "0",
            "The frequency shift in Hz until a number or a signal reaches the shift inlet: "
            "above 0 up, below 0 down.",
            "Hz");
  PARAM_DOC("bins", "4096",
            "The largest frame kept, in bins: half the FFT size of the ~pfft it runs in, or the "
            "FFT size with the full spectrum. Bins past it put out 0. Changing it rebuilds the "
            "object.",
            "1-65536");
}

// Control thread: a capacity change rebuilds the object, so the storage is
// allocated on one no audio thread has seen.
PARM_CLEAR() {
  shift = 0.f;
  bins = static_cast<int>(spectralFrameStore::DEFAULT_CAPACITY);
  numbers[2] = 0.f;
  if (store.capacity() != spectralFrameStore::DEFAULT_CAPACITY) {
    store = spectralFrameStore(2, 2);
    fn = spectralFreqShift();
  }
}

PARM_PARSE() {
  const UInt capacity = Between(bins, 1, spectralFrameStore::MAXIMUM_CAPACITY);
  if (store.capacity() != capacity) {
    store = spectralFrameStore(2, 2, capacity);
    fn = spectralFreqShift(0.f, capacity);
  }
  bins = static_cast<int>(capacity);
  numbers[2] = shift;
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
  // 'clip <0|1>'; anything else is ignored.
  const char* text = message.c_str();
  const std::size_t size = message.size();
  std::size_t next = 0;
  const std::string_view word = Token(text, size, 0, next);
  const std::string_view argument = Token(text, size, next, next);
  float number = 0.f;
  if (argument.empty() || !ReadNumericToken(argument.data(), argument.size(), number)) return;
  if (word == "clip") clipping = number != 0.f;
}

// Outside a ~pfft: the inputs through. Inside: frame-wide (#1280), the
// pass's bins stored on the consume step, the frame shifted at its first
// emit step and handed out a pass at a time.
CALC() {
  const spectralPass* pass = CurrentSpectralPass();
  if (pass == nullptr) {
    for (int c = 0; c < 2; ++c) {
      Copy(inputs[c].Signal(numbers[c].load()), through[c]);
      outputs[c].SendBuffer(&through[c], thread);
    }
    return;
  }
  if (!pass->emitting) {
    if (pass->pass == 0) {
      DSP::buffer& s = inputs[2].Signal(numbers[2].load());
      frameShift = s.getLength() > 0 ? s.getPtr()[0] : numbers[2].load();
    }
    store.consume(*pass, 0, inputs[0].Signal(numbers[0].load()));
    store.consume(*pass, 1, inputs[1].Signal(numbers[1].load()));
    return;
  }
  if (store.due()) {
    fn.frequencyShift(frameShift).clip(clipping.load());
    fn(store.frame(*pass), store.input(0), store.input(1), store.output(0), store.output(1));
  }
  outputs[0].SendBuffer(&store.slice(*pass, 0), thread);
  outputs[1].SendBuffer(&store.slice(*pass, 1), thread);
}

#undef className
