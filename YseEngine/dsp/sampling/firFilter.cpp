/*
  ==============================================================================

    firFilter.cpp
    A FIR filter whose taps are read from a sample buffer (issue #1149).

  ==============================================================================
*/

#include "firFilter.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>

using namespace YSE::DSP;

namespace {

  constexpr auto HISTORY = static_cast<UInt>(firFilter::MAX_TAPS);

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

} // namespace

firFilter::firFilter()
  : history(static_cast<std::size_t>(HISTORY) * 2, 0.f),
    kernel(HISTORY, 0.f),
    output(YSE::STANDARD_BUFFERSIZE) {}

firFilter& firFilter::clear() {
  std::fill(history.begin(), history.end(), 0.f);
  return *this;
}

void firFilter::loadTaps(const sampleView& view) {
  usedTaps = 0;
  const Flt* samples = view.channel(targetChannel);
  const UInt frames = view.frames();
  // offset and size are >= 0 (the setters clamp them).
  const auto begin = static_cast<UInt>(firstFrame);
  if (samples == nullptr || begin >= frames) return;
  const UInt count = std::min(static_cast<UInt>(tapCount), frames - begin);
  // Within MAX_TAPS (the setter holds the size there), so in `kernel`.
  Flt* taps = kernel.data();
  for (UInt k = 0; k < count; ++k)
    taps[k] = detail::LoadSample(samples + begin + k);
  usedTaps = count;
}

inline Flt firFilter::step(Flt x) {
  write = (write == 0 ? HISTORY : write) - 1;
  Flt* past = history.data() + write;
  past[0] = x;
  past[HISTORY] = x;
  const Flt* taps = kernel.data();
  Flt sum = 0.f;
  for (UInt k = 0; k < usedTaps; ++k)
    sum += taps[k] * past[k];
  return sum;
}

buffer& firFilter::operator()(const sampleView& view, buffer& in) {
  const UInt length = BlockLength(in.getLength());
  // Within the capacity set in the constructor: no allocation.
  if (output.getLength() != length) output.resize(length);
  loadTaps(view);
  Flt* out = output.getPtr();
  const Flt* x = in.getPtr();
  for (UInt i = 0; i < length; ++i)
    out[i] = step(x[i]);
  return output;
}

buffer& firFilter::operator()(const sampleView& view, Flt x, UInt length) {
  length = BlockLength(length);
  if (output.getLength() != length) output.resize(length);
  loadTaps(view);
  Flt* out = output.getPtr();
  for (UInt i = 0; i < length; ++i)
    out[i] = step(x);
  return output;
}
