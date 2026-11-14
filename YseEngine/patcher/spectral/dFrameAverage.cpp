#include "dFrameAverage.h"
#include "../pSelector.h"
#include "spectralPass.h"

#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::frameAverage;
using YSE::DSP::frameSmooth;

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

  // A parameter held between `lowest` and `highest`.
  UInt Between(int value, UInt lowest, UInt highest) {
    if (value < static_cast<int>(lowest)) return lowest;
    const auto v = static_cast<UInt>(value);
    return v > highest ? highest : v;
  }

  // The audio thread's block. A clear waits for the start of a frame: the
  // first pass inside a ~pfft, any block outside one. Inside a ~pfft the
  // block is the pass's run of bins; outside, the class cuts the stream.
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

  constexpr const char* FRAMES_IN =
      "The frames, one value per bin: inside a ~pfft its spectral frames, elsewhere frames of "
      "framesize samples, or every block with framesize 0. A signal; unconnected, the last "
      "number sent here (0).";
  constexpr const char* FRAMESIZE_DOC =
      "Outside a ~pfft, the frame length in samples: sample k of a frame is bin k, the frames "
      "running on across blocks (as a ~fft puts out its frames). 0 makes every block a frame. "
      "Inside a ~pfft the frames are the ~pfft's and this is not used. Changing it rebuilds the "
      "object.";
} // namespace

// ─── ~frameaverage ────────────────────────────────────────────────────────────

#define className dFrameaverage

CONSTRUCT_DSP() {
  // in 0: the frames; clear
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the average per bin
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(framecount);
  ADD_PARAM(bins);
  ADD_PARAM(framesize);

  ADD_DESCRIPTION("The running average of every bin over the last frames: each value's mean "
                  "with the values at the same place in the frames before. On a ~pfft's "
                  "magnitudes, a spectrum smoothed in time.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "in", std::string(FRAMES_IN) + " 'clear' sets the stored frames to 0.", "");
  OUTLET_DOC(0, "out",
             "Each bin's mean over this frame and the framecount - 1 frames before it, which "
             "count as 0 at the start and after a clear. 0 for bins past the bins parameter.",
             "");
  PARAM_DOC("framecount", "4",
            "How many frames are averaged, the current one included. Changing it rebuilds the "
            "object.",
            "1-256");
  PARAM_DOC("bins", "4096",
            "The largest frame kept, in bins: half the FFT size of the ~pfft it runs in, or the "
            "FFT size with the full spectrum. Bins past it put out 0. Changing it rebuilds the "
            "object.",
            "1-65536");
  PARAM_DOC("framesize", "0", FRAMESIZE_DOC, "0-65536");
}

// Control thread: the clear and parse callbacks make a live change rebuild
// the object, so the storage is allocated on one no audio thread has seen.
PARM_CLEAR() {
  framecount = static_cast<int>(frameAverage::DEFAULT_FRAMES);
  bins = static_cast<int>(frameAverage::DEFAULT_CAPACITY);
  framesize = 0;
  if (fn.maxFrames() != frameAverage::DEFAULT_FRAMES ||
      fn.capacity() != frameAverage::DEFAULT_CAPACITY)
    fn = frameAverage();
  fn.frameSize(0);
}

PARM_PARSE() {
  const UInt frames = Between(framecount, 1, frameAverage::MAXIMUM_FRAMES);
  const UInt capacity = Between(bins, 1, frameAverage::MAXIMUM_CAPACITY);
  const UInt size = Between(framesize, 0, frameAverage::MAXIMUM_FRAME_SIZE);
  if (fn.maxFrames() != frames || fn.capacity() != capacity) fn = frameAverage(frames, capacity);
  fn.frameCount(frames);
  fn.frameSize(size);
  framecount = static_cast<int>(frames);
  bins = static_cast<int>(capacity);
  framesize = static_cast<int>(size);
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

// ─── ~framesmooth ─────────────────────────────────────────────────────────────

#define className dFramesmooth

CONSTRUCT_DSP() {
  // in 0: the frames; clear
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the frames smoothed along themselves
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(smoothness);
  ADD_PARAM(framesize);

  ADD_DESCRIPTION("A running average along every frame that never reaches into the next: each "
                  "value's mean with the values just before it in the same frame. On a ~pfft's "
                  "magnitudes, a spectrum smoothed across frequency.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "in",
            std::string(FRAMES_IN) + " 'clear' forgets the values before, so the next starts a "
                                     "new window.",
            "");
  OUTLET_DOC(0, "out",
             "Each value's mean with the smoothness - 1 values before it in its frame; at the "
             "start of a frame, with as many as there are, so the first comes out as it goes in.",
             "");
  PARAM_DOC("smoothness", "3",
            "How many values are averaged, the current one included. The window trails the "
            "value, so a peak moves up by (smoothness - 1) / 2 bins. Changing it rebuilds the "
            "object.",
            "1-1024");
  PARAM_DOC("framesize", "0", FRAMESIZE_DOC, "0-65536");
}

PARM_CLEAR() {
  smoothness = static_cast<int>(frameSmooth::DEFAULT_SMOOTHNESS);
  framesize = 0;
  fn.smoothness(frameSmooth::DEFAULT_SMOOTHNESS);
  fn.frameSize(0);
}

PARM_PARSE() {
  const UInt values = Between(smoothness, 1, frameSmooth::MAXIMUM_SMOOTHNESS);
  const UInt size = Between(framesize, 0, frameSmooth::MAXIMUM_FRAME_SIZE);
  fn.smoothness(values);
  fn.frameSize(size);
  smoothness = static_cast<int>(values);
  framesize = static_cast<int>(size);
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
