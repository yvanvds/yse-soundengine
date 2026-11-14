#include "dFft.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::streamingTransform;

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

  // `phase <n>` or `offset <n>`: the offset in samples, -1 for any other
  // message or a number that is not a finite one at or above 0.
  int OffsetMessage(const std::string& message) {
    std::size_t next = 0;
    const std::string_view word = Token(message, 0, next);
    if (word != "phase" && word != "offset") return -1;
    const std::string_view arg = Token(message, next, next);
    float number = 0.f;
    if (!ReadNumericToken(arg.data(), arg.size(), number)) return -1;
    if (!(number >= 0.f) || !std::isfinite(number)) return -1;
    const auto most = static_cast<float>(streamingTransform::MAXIMUM_INTERVAL);
    return number >= most ? static_cast<int>(streamingTransform::MAXIMUM_INTERVAL)
                          : static_cast<int>(number);
  }

  UInt Argument(int value) {
    return value < 0 ? 0u : static_cast<UInt>(value);
  }

  // Control thread: a new transform when the arguments, fitted, differ
  // from the one there is. Allocates only then.
  template <class Transform> void Configure(Transform& fn, int size, int interval, int offset) {
    const UInt frame = Transform::fitSize(Argument(size));
    const UInt period = Transform::fitInterval(Argument(interval), frame);
    const UInt start = Argument(offset) % period;
    if (fn.size() != frame || fn.interval() != period || fn.offset() != start)
      fn = Transform(frame, period, start);
  }

  // The audio thread's block: a pending offset, the inputs (an unconnected
  // imaginary inlet at 0 is a real input), the outlets right to left.
  template <class Transform>
  void Block(Transform& fn, std::atomic<int>& pendingOffset, std::atomic<float> (&numbers)[2],
             std::vector<inlet>& inputs, std::vector<outlet>& outputs, YSE::THREAD thread) {
    const int start = pendingOffset.exchange(-1);
    if (start >= 0) fn.offset(static_cast<UInt>(start));

    // Unconnected inlets read their numbers (#1036).
    YSE::DSP::buffer& real = inputs[0].Signal(numbers[0].load());
    const float imaginary = numbers[1].load();
    if (inputs[1].IsConstant() && imaginary == 0.f)
      fn(real);
    else
      fn(real, inputs[1].Signal(imaginary));

    outputs[2].SendBuffer(&fn.sync(), thread);
    outputs[1].SendBuffer(&fn.imaginary(), thread);
    outputs[0].SendBuffer(&fn.real(), thread);
  }
} // namespace

// ─── ~fft ─────────────────────────────────────────────────────────────────────

#define className dFft

CONSTRUCT_DSP() {
  // in 0: the real input; phase / offset
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the imaginary input
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0, 1, 2: real, imaginary, sync
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(size);
  ADD_PARAM(interval);
  ADD_PARAM(offset);

  ADD_DESCRIPTION("A Fourier transform of a signal, frame by frame: every interval samples, a "
                  "frame of size samples is transformed and its spectrum put out over the next "
                  "size samples, one bin per sample, as real and imaginary signals with the bin "
                  "index on the sync outlet.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "real",
            "The signal to transform (its real part). A signal; unconnected, the last number "
            "sent here (0). Also takes 'phase <n>' or 'offset <n>': a new offset in samples, "
            "taken modulo the interval, from the next block on (not saved).",
            "");
  INLET_DOC(1, "imaginary",
            "The imaginary part of the input, for a complex transform. A signal; unconnected, "
            "the last number sent here (0), which transforms a real signal.",
            "");
  OUTLET_DOC(0, "real",
             "The real part of the spectrum: bin k at sample k of each output period, which "
             "starts the sample after a frame's last input sample. Bin k is k * samplerate / "
             "size Hz; the bins above size / 2 mirror those below for a real input. Not "
             "divided by the size; 0 outside an output period.",
             "");
  OUTLET_DOC(1, "imaginary", "The imaginary part of the spectrum, timed as the real part.", "");
  OUTLET_DOC(2, "sync",
             "The index of the bin put out, 0 to size - 1, for reading other frames in step "
             "(~index); 0 outside an output period.",
             "0 to size - 1");
  PARAM_DOC("size", "512",
            "The frame size in samples, rounded up to a power of two. Changing it rebuilds the "
            "object.",
            "16-65536");
  PARAM_DOC("interval", "0",
            "Samples from the start of one frame to the next, rounded up to a power of two at "
            "or above the size; 0 means the size, frames back to back. With a larger interval "
            "the samples after each frame are not used. Changing it rebuilds the object.",
            "0, size-16777216");
  PARAM_DOC("offset", "0",
            "Samples into the stream that the first frame starts, taken modulo the interval: "
            "staggers several ~fft objects for overlapping frames. Changing it rebuilds the "
            "object.",
            "0 to interval - 1");
}

// Control thread: the clear and parse callbacks make a live change rebuild
// the object, so the frame storage is allocated on one no audio thread has
// seen. Clear runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  size = static_cast<int>(streamingTransform::DEFAULT_SIZE);
  interval = 0;
  offset = 0;
  Configure(fn, size, interval, offset);
}

PARM_PARSE() {
  Configure(fn, size, interval, offset);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet >= 0 && inlet < 2) numbers[inlet] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  const int start = OffsetMessage(message);
  if (start >= 0) pendingOffset = start;
}

CALC() {
  Block(fn, pendingOffset, numbers, inputs, outputs, thread);
}

#undef className

// ─── ~ifft ────────────────────────────────────────────────────────────────────

#define className dIfft

CONSTRUCT_DSP() {
  // in 0: the real bins; phase / offset
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the imaginary bins
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0, 1, 2: real, imaginary, sync
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(size);
  ADD_PARAM(interval);
  ADD_PARAM(offset);

  ADD_DESCRIPTION("An inverse Fourier transform of a stream of bins, frame by frame: every "
                  "interval samples, a frame of size bins (bin k at sample k) is transformed back "
                  "and put out over the next size samples, as real and imaginary signals. "
                  "Resynthesises what ~fft takes apart.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "real",
            "The real part of the bins, bin k at sample k of a frame. A signal; unconnected, "
            "the last number sent here (0). Also takes 'phase <n>' or 'offset <n>': a new "
            "offset in samples, taken modulo the interval, from the next block on (not saved).",
            "");
  INLET_DOC(1, "imaginary",
            "The imaginary part of the bins. A signal; unconnected, the last number sent here "
            "(0).",
            "");
  OUTLET_DOC(0, "real",
             "The real part of the result, sample k at sample k of each output period, which "
             "starts the sample after a frame's last bin. Not divided by the size: after a "
             "~fft with the same arguments it is the input size times larger, 2 * size samples "
             "later. 0 outside an output period.",
             "");
  OUTLET_DOC(1, "imaginary",
             "The imaginary part of the result, timed as the real part: 0 for the bins of a "
             "real signal.",
             "");
  OUTLET_DOC(2, "sync",
             "The index of the sample put out, 0 to size - 1; 0 outside an output period.",
             "0 to size - 1");
  PARAM_DOC("size", "512",
            "The frame size in bins, rounded up to a power of two. Changing it rebuilds the "
            "object.",
            "16-65536");
  PARAM_DOC("interval", "0",
            "Samples from the start of one frame to the next, rounded up to a power of two at "
            "or above the size; 0 means the size. After a ~fft with a larger interval, use an "
            "offset size samples later than the ~fft's. Changing it rebuilds the object.",
            "0, size-16777216");
  PARAM_DOC("offset", "0",
            "Samples into the stream that the first frame starts, taken modulo the interval. "
            "Changing it rebuilds the object.",
            "0 to interval - 1");
}

PARM_CLEAR() {
  size = static_cast<int>(streamingTransform::DEFAULT_SIZE);
  interval = 0;
  offset = 0;
  Configure(fn, size, interval, offset);
}

PARM_PARSE() {
  Configure(fn, size, interval, offset);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet >= 0 && inlet < 2) numbers[inlet] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  const int start = OffsetMessage(message);
  if (start >= 0) pendingOffset = start;
}

CALC() {
  Block(fn, pendingOffset, numbers, inputs, outputs, thread);
}

#undef className
