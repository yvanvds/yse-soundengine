#include "dFrameDelta.h"
#include "../pSelector.h"
#include "spectralPass.h"

#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::frameAccumulate;
using YSE::DSP::frameDelta;

namespace {
  // Whether `message` is the word `clear`, walked in place so nothing
  // allocates on whichever thread delivers it.
  bool IsClear(const std::string& message) {
    const char* chars = message.c_str();
    const std::size_t size = message.size();
    std::size_t begin = 0;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    return std::string_view(chars + begin, end - begin) == "clear";
  }

  // The bins argument as a capacity: 1 to the classes' maximum.
  UInt Capacity(int value) {
    if (value < 1) return 1u;
    const auto bins = static_cast<UInt>(value);
    return bins > frameDelta::MAXIMUM_CAPACITY ? frameDelta::MAXIMUM_CAPACITY : bins;
  }

  // The audio thread's block. A clear waits for the start of a frame: the
  // first pass inside a ~pfft, any block outside one. Inside a ~pfft the
  // block is the pass's run of bins; outside, a frame of its own.
  template <class Fn>
  void Block(Fn& fn, std::atomic<float>& number, std::atomic<bool>& clearPending,
             std::vector<inlet>& inputs, std::vector<outlet>& outputs, YSE::THREAD thread) {
    const spectralPass* pass = CurrentSpectralPass();
    if ((pass == nullptr || pass->pass == 0) && clearPending.exchange(false)) fn.clear();
    // An unconnected inlet reads its number (#1036).
    YSE::DSP::buffer& in = inputs[0].Signal(number.load());
    YSE::DSP::buffer& out = pass != nullptr ? fn(pass->Frame(in.getPtr(), nullptr), in) : fn(in);
    outputs[0].SendBuffer(&out, thread);
  }
} // namespace

// ─── ~framedelta ──────────────────────────────────────────────────────────────

#define className dFramedelta

CONSTRUCT_DSP() {
  // in 0: the frames; clear
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the change per bin
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(bins);

  ADD_DESCRIPTION("The change of every bin since the previous frame: each value minus the value "
                  "at the same place in the frame before. On the phases of a ~pfft's frames, the "
                  "phase deviation of a phase vocoder.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "in",
            "The frames, one value per bin: inside a ~pfft its spectral frames, elsewhere every "
            "block counts as a frame. A signal; unconnected, the last number sent here (0). "
            "'clear' sets the stored frame to 0.",
            "");
  OUTLET_DOC(0, "out",
             "Each value minus the same bin of the frame before; the first frame, and the first "
             "after a clear, as it comes in. 0 for bins past the bins parameter.",
             "");
  PARAM_DOC("bins", "4096",
            "The largest frame kept, in bins: half the FFT size of the ~pfft it runs in, or the "
            "FFT size with the full spectrum. Bins past it put out 0. Changing it rebuilds the "
            "object.",
            "1-65536");
}

// Control thread: the clear and parse callbacks make a live change rebuild
// the object, so the storage is allocated on one no audio thread has seen.
PARM_CLEAR() {
  bins = static_cast<int>(frameDelta::DEFAULT_CAPACITY);
  if (fn.capacity() != frameDelta::DEFAULT_CAPACITY) fn = frameDelta();
}

PARM_PARSE() {
  const UInt capacity = Capacity(bins);
  if (fn.capacity() != capacity) fn = frameDelta(capacity);
  bins = static_cast<int>(capacity);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0) number = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  if (IsClear(message)) clearPending = true;
}

CALC() {
  Block(fn, number, clearPending, inputs, outputs, thread);
}

#undef className

// ─── ~frameaccum ──────────────────────────────────────────────────────────────

#define className dFrameaccum

CONSTRUCT_DSP() {
  // in 0: the frames; clear
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the running sum per bin
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(wrap);
  ADD_PARAM(bins);

  ADD_DESCRIPTION("A running sum of every bin over successive frames: each value plus the sum "
                  "at the same place over the frames before, optionally wrapped into -pi to pi. "
                  "On a ~framedelta's phase deviations, the running phase of a phase vocoder.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "in",
            "The frames, one value per bin: inside a ~pfft its spectral frames, elsewhere every "
            "block counts as a frame. A signal; unconnected, the last number sent here (0). "
            "'clear' sets every sum to 0.",
            "");
  OUTLET_DOC(0, "out",
             "Each value plus the sum of the same bin over the frames before; with wrap on, "
             "wrapped into -pi to pi. 0 for bins past the bins parameter.",
             "");
  PARAM_DOC("wrap", "0",
            "Nonzero wraps the sums into -pi to pi and keeps them there, as a ~phasewrap after "
            "the object would without the sums growing and losing precision. Changing it "
            "rebuilds the object.",
            "0 or 1");
  PARAM_DOC("bins", "4096",
            "The largest frame kept, in bins: half the FFT size of the ~pfft it runs in, or the "
            "FFT size with the full spectrum. Bins past it put out 0. Changing it rebuilds the "
            "object.",
            "1-65536");
}

PARM_CLEAR() {
  wrap = 0;
  bins = static_cast<int>(frameAccumulate::DEFAULT_CAPACITY);
  if (fn.capacity() != frameAccumulate::DEFAULT_CAPACITY) fn = frameAccumulate();
  fn.wrap(false);
}

PARM_PARSE() {
  const UInt capacity = Capacity(bins);
  if (fn.capacity() != capacity) fn = frameAccumulate(capacity);
  bins = static_cast<int>(capacity);
  wrap = wrap != 0 ? 1 : 0;
  fn.wrap(wrap != 0);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0) number = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  if (IsClear(message)) clearPending = true;
}

CALC() {
  Block(fn, number, clearPending, inputs, outputs, thread);
}

#undef className
