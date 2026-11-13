/*
  ==============================================================================

    transferLookup.cpp
    A sample buffer used as a transfer function: a signal from -1 to 1 looks
    up a value in it (issue #1147).

  ==============================================================================
*/

#include "transferLookup.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>

using namespace YSE::DSP;

namespace {

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // The entries of the table: `count` samples from `first`. Empty (null
  // `first`) without a channel, a buffer or an entry.
  struct table {
    const Flt* first = nullptr;
    UInt count = 0;
  };

  table TableOf(const sampleView& view, UInt channel, Int offset, Int size) {
    table t;
    const Flt* samples = view.channel(channel);
    const UInt frames = view.frames();
    if (samples == nullptr || frames == 0) return t;
    // offset and size are >= 0 (the setters clamp them).
    const UInt end = std::min(static_cast<UInt>(size), frames);
    const auto begin = static_cast<UInt>(offset);
    if (begin >= end) return t;
    t.first = samples + begin;
    t.count = end - begin;
    return t;
  }

  // The value at input `x`: -1 is entry 0, 1 the last entry, a line between
  // two entries. 0 outside -1 to 1 (NaN included). `t` has entries.
  inline Flt Lookup(const table& t, Flt x) {
    if (!(x >= -1.f && x <= 1.f)) return 0.f;
    const UInt last = t.count - 1;
    const Dbl position = (static_cast<Dbl>(x) + 1.0) * 0.5 * static_cast<Dbl>(last);
    auto index = static_cast<UInt>(position);
    if (index >= last) return detail::LoadSample(t.first + last);
    const auto frac = static_cast<Flt>(position - static_cast<Dbl>(index));
    const Flt here = detail::LoadSample(t.first + index);
    const Flt next = detail::LoadSample(t.first + index + 1);
    return here + (next - here) * frac;
  }

} // namespace

transferLookup::transferLookup() : output(YSE::STANDARD_BUFFERSIZE) {}

Flt transferLookup::read(const sampleView& view, Flt x) const {
  const table t = TableOf(view, targetChannel, firstFrame, endFrame);
  return t.first == nullptr ? 0.f : Lookup(t, x);
}

buffer& transferLookup::operator()(const sampleView& view, buffer& in) {
  const UInt length = BlockLength(in.getLength());
  // Within the capacity set in the constructor: no allocation.
  if (output.getLength() != length) output.resize(length);
  Flt* out = output.getPtr();
  const table t = TableOf(view, targetChannel, firstFrame, endFrame);
  if (t.first == nullptr) {
    std::fill_n(out, length, 0.f);
    return output;
  }
  const Flt* x = in.getPtr();
  for (UInt i = 0; i < length; ++i)
    out[i] = Lookup(t, x[i]);
  return output;
}

buffer& transferLookup::operator()(const sampleView& view, Flt x, UInt length) {
  length = BlockLength(length);
  if (output.getLength() != length) output.resize(length);
  std::fill_n(output.getPtr(), length, read(view, x));
  return output;
}
