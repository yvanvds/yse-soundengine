/*
  ==============================================================================

    waveReader.cpp
    A region of a sample buffer read as one cycle of a waveform, at a phase
    from 0 to 1, in one row or crossfaded across rows (issue #1146).

  ==============================================================================
*/

#include "waveReader.hpp"
#include "sampleBuffer.hpp"
#include "sampleRegion.hpp"

#include <algorithm>
#include <cmath>

using namespace YSE::DSP;

namespace {

  constexpr Dbl PI = 3.14159265358979323846;

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // The rows of one block: `count` rows of `length` frames from `first`.
  // No rows (length 0) without a region long enough for one frame a row.
  struct layout {
    UInt first = 0;
    UInt length = 0;
    UInt count = 1;
  };

  layout LayoutOf(const sampleView& view, Flt startMs, Flt endMs, UInt rows) {
    layout l;
    if (view.empty()) return l;
    const sampleRegion span =
        sampleRegion::fromMs(startMs, endMs, view.sampleRate(), view.frames());
    l.count = rows > 1 ? rows : 1;
    l.first = span.start;
    l.length = span.length() / l.count;
    return l;
  }

  // Where a phase falls in a row of `length` frames: frame `index` and the
  // fraction towards the next. False for a phase that is not finite.
  struct position {
    UInt index = 0;
    Flt frac = 0.f;
  };

  inline bool PositionOf(Flt phase, UInt length, position& at) {
    if (!std::isfinite(phase)) return false;
    Dbl wrapped = static_cast<Dbl>(phase) - std::floor(static_cast<Dbl>(phase));
    Dbl p = wrapped * static_cast<Dbl>(length);
    auto index = static_cast<UInt>(p);
    if (index >= length) { // rounding at the top of the cycle: back to 0
      index = 0;
      p = 0.0;
    }
    at.index = index;
    at.frac = static_cast<Flt>(p - static_cast<Dbl>(index));
    return true;
  }

  // Which two rows a row phase falls between, and the weight of the second.
  struct rowPick {
    UInt lower = 0;
    UInt upper = 0;
    Flt weight = 0.f;
  };

  inline rowPick RowsOf(Flt rowPhase, UInt count) {
    rowPick pick;
    if (count < 2 || !(rowPhase > 0.f)) return pick; // NaN too
    const UInt last = count - 1;
    if (rowPhase >= 1.f) {
      pick.lower = last;
      pick.upper = last;
      return pick;
    }
    const Dbl r = static_cast<Dbl>(rowPhase) * static_cast<Dbl>(last);
    auto lower = static_cast<UInt>(r);
    if (lower >= last) lower = last;
    pick.lower = lower;
    pick.upper = lower < last ? lower + 1 : last;
    pick.weight = static_cast<Flt>(r - static_cast<Dbl>(lower));
    return pick;
  }

  // One row of `length` frames from `row`, at `at`, with the neighbours
  // wrapping around the row.
  Flt ReadRow(const Flt* row, UInt length, const position& at, waveReader::interpolation mode,
              Flt bias, Flt tension) {
    const UInt i = at.index;
    const Flt f = at.frac;
    const Flt y1 = detail::LoadSample(row + i);
    if (mode == waveReader::interpolation::NONE) return y1;
    const UInt next = i + 1 < length ? i + 1 : 0;
    const Flt y2 = detail::LoadSample(row + next);
    switch (mode) {
    case waveReader::interpolation::COSINE: {
      const auto g = static_cast<Flt>((1.0 - std::cos(static_cast<Dbl>(f) * PI)) * 0.5);
      return y1 + (y2 - y1) * g;
    }
    case waveReader::interpolation::CUBIC:
    case waveReader::interpolation::SPLINE:
    case waveReader::interpolation::HERMITE:
      break;
    default: // LINEAR, LINEAR_LOW
      return y1 + (y2 - y1) * f;
    }
    const UInt before = i > 0 ? i - 1 : length - 1;
    const UInt after = next + 1 < length ? next + 1 : 0;
    const Flt y0 = detail::LoadSample(row + before);
    const Flt y3 = detail::LoadSample(row + after);
    const Flt f2 = f * f;
    if (mode == waveReader::interpolation::CUBIC) {
      const Flt a0 = y3 - y2 - y0 + y1;
      const Flt a1 = y0 - y1 - a0;
      const Flt a2 = y2 - y0;
      return a0 * f * f2 + a1 * f2 + a2 * f + y1;
    }
    if (mode == waveReader::interpolation::SPLINE) {
      const Flt a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
      const Flt a1 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
      const Flt a2 = -0.5f * y0 + 0.5f * y2;
      return a0 * f * f2 + a1 * f2 + a2 * f + y1;
    }
    // HERMITE
    const Flt k = (1.f - tension) * 0.5f;
    const Flt m0 = ((y1 - y0) * (1.f + bias) + (y2 - y1) * (1.f - bias)) * k;
    const Flt m1 = ((y2 - y1) * (1.f + bias) + (y3 - y2) * (1.f - bias)) * k;
    const Flt f3 = f2 * f;
    const Flt h0 = 2.f * f3 - 3.f * f2 + 1.f;
    const Flt h1 = f3 - 2.f * f2 + f;
    const Flt h2 = f3 - f2;
    const Flt h3 = -2.f * f3 + 3.f * f2;
    return h0 * y1 + h1 * m0 + h2 * m1 + h3 * y2;
  }

  // Channel `c` at a phase and row pick. `l` has rows.
  inline Flt ReadAt(const sampleView& view, UInt c, const layout& l, const position& at,
                    const rowPick& pick, waveReader::interpolation mode, Flt bias, Flt tension) {
    const Flt* samples = view.channel(c);
    if (samples == nullptr) return 0.f;
    const Flt* region = samples + l.first;
    const Flt v = ReadRow(region + static_cast<std::size_t>(pick.lower) * l.length, l.length, at,
                          mode, bias, tension);
    if (pick.weight <= 0.f) return v;
    const Flt w = ReadRow(region + static_cast<std::size_t>(pick.upper) * l.length, l.length, at,
                          mode, bias, tension);
    return v + (w - v) * pick.weight;
  }

} // namespace

waveReader::waveReader(UInt count) {
  channels(count);
}

waveReader& waveReader::channels(UInt count) {
  count = std::clamp<UInt>(count, 1, MAX_CHANNELS);
  outputs.clear();
  outputs.reserve(count);
  for (UInt c = 0; c < count; ++c)
    outputs.emplace_back(YSE::STANDARD_BUFFERSIZE);
  return *this;
}

Flt waveReader::read(const sampleView& view, UInt channel, Flt phase, Flt rowPhase) const {
  const layout l = LayoutOf(view, startMs, endMs, rowCount);
  position at;
  if (l.length == 0 || !PositionOf(phase, l.length, at)) return 0.f;
  return ReadAt(view, channel, l, at, RowsOf(rowPhase, l.count), interpolate, hermiteBias,
                hermiteTension);
}

buffer& waveReader::operator()(const sampleView& view, buffer& phase) {
  return Render(view, phase.getPtr(), nullptr, BlockLength(phase.getLength()));
}

buffer& waveReader::operator()(const sampleView& view, buffer& phase, buffer& rowPhase) {
  const UInt length = BlockLength(std::min(phase.getLength(), rowPhase.getLength()));
  return Render(view, phase.getPtr(), rowPhase.getPtr(), length);
}

buffer& waveReader::Render(const sampleView& view, const Flt* phase, const Flt* rowPhase,
                           UInt length) {
  for (buffer& out : outputs) {
    // Within the capacity set by channels(): no allocation.
    if (out.getLength() != length) out.resize(length);
    std::fill_n(out.getPtr(), length, 0.f);
  }
  const layout l = LayoutOf(view, startMs, endMs, rowCount);
  if (l.length == 0) return outputs[0];
  // Only the outputs the buffer has a channel for; the rest stay silent.
  const auto reading = static_cast<UInt>(std::min<std::size_t>(outputs.size(), view.channels()));
  const rowPick oneRow;
  for (UInt i = 0; i < length; ++i) {
    position at;
    if (!PositionOf(phase[i], l.length, at)) continue;
    const rowPick pick = rowPhase != nullptr ? RowsOf(rowPhase[i], l.count) : oneRow;
    for (UInt c = 0; c < reading; ++c)
      outputs[c].getPtr()[i] =
          ReadAt(view, c, l, at, pick, interpolate, hermiteBias, hermiteTension);
  }
  return outputs[0];
}
