/*
  ==============================================================================

    tableLookup.cpp
    A sample buffer used as a lookup table at signal rate (issue #1148).

  ==============================================================================
*/

#include "tableLookup.hpp"
#include "../generators/whiteSource.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>
#include <cmath>

using namespace YSE::DSP;

namespace {

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // `value` in [0, frames): what the table repeated reaches. `frames` > 0.
  inline Dbl Wrap(Dbl value, UInt frames) {
    const auto size = static_cast<Dbl>(frames);
    Dbl wrapped = std::fmod(value, size);
    if (wrapped < 0.0) wrapped += size;
    // fmod of a tiny negative can round up to `size` itself.
    return wrapped < size ? wrapped : 0.0;
  }

} // namespace

tableLookup::tableLookup() : state(detail::WHITE_DEFAULT_SEED), output(YSE::STANDARD_BUFFERSIZE) {
  // A new table reads from the start: the first `next` or trigger reads
  // entry 0, as after a `goto 0`.
  gotoWaiting = true;
}

tableLookup& tableLookup::inputMap(Flt lo, Flt hi) {
  mapIn = std::isfinite(lo) && std::isfinite(hi) && lo != hi;
  if (mapIn) {
    inLo = lo;
    inHi = hi;
  }
  return *this;
}

tableLookup& tableLookup::outputScale(Flt lo, Flt hi) {
  scaleOut = std::isfinite(lo) && std::isfinite(hi) && lo != hi;
  if (scaleOut) {
    outLo = lo;
    outHi = hi;
  }
  return *this;
}

tableLookup::table tableLookup::Table(const sampleView& view) const {
  table t;
  const Flt* samples = view.channel(targetChannel);
  if (samples != nullptr && view.frames() > 0) {
    t.samples = samples;
    t.frames = view.frames();
  }
  return t;
}

Dbl tableLookup::MapIn(Dbl value, UInt frames) const {
  if (!mapIn) return value;
  return (value - inLo) / (inHi - inLo) * static_cast<Dbl>(frames);
}

Flt tableLookup::ScaleOut(Flt value) const {
  if (!scaleOut) return value;
  const Dbl lo = isSigned ? -static_cast<Dbl>(valueRange) : 0.0;
  const Dbl hi = isSigned ? static_cast<Dbl>(valueRange) : static_cast<Dbl>(valueRange - 1);
  if (hi == lo) return outLo; // range 1, unsigned: a single value
  return static_cast<Flt>(outLo + (value - lo) / (hi - lo) * (static_cast<Dbl>(outHi) - outLo));
}

Flt tableLookup::Read(const table& t, Dbl index) {
  if (t.samples == nullptr) return 0.f;
  const UInt frames = t.frames;
  const auto last = static_cast<Dbl>(frames - 1);

  // An index that is not a number is outside every table.
  if (std::isnan(index)) {
    switch (edge) {
    case outOfRange::ZERO:
      return held = 0.f;
    case outOfRange::HOLD:
      return held;
    default:
      return held = detail::LoadSample(t.samples);
    }
  }
  if (edge == outOfRange::WRAP) index = std::isfinite(index) ? Wrap(index, frames) : 0.0;

  Dbl whole = 0.0;
  Dbl fraction = 0.0;
  switch (interpolate) {
  case interpolation::NONE:
    whole = std::floor(index);
    break;
  case interpolation::ROUND:
    whole = std::floor(index + 0.5);
    break;
  case interpolation::LINEAR:
    whole = std::floor(index);
    fraction = index - whole;
    break;
  }

  // One entry, by the out-of-range rule. False only for HOLD outside.
  auto entry = [&](Dbl at, Flt& value) {
    if (at >= 0.0 && at <= last) {
      value = detail::LoadSample(t.samples + static_cast<std::size_t>(at));
      return true;
    }
    switch (edge) {
    case outOfRange::ZERO:
      value = 0.f;
      return true;
    case outOfRange::EXTEND:
      value = detail::LoadSample(t.samples + (at < 0.0 ? 0 : frames - 1));
      return true;
    case outOfRange::WRAP: // only the entry past the last
      value = detail::LoadSample(t.samples);
      return true;
    case outOfRange::HOLD:
      break;
    }
    return false;
  };

  Flt a = 0.f;
  if (!entry(whole, a)) return held;
  if (fraction > 0.0) {
    Flt b = 0.f;
    if (!entry(whole + 1.0, b)) return held;
    a = static_cast<Flt>(a + fraction * (static_cast<Dbl>(b) - a));
  }
  return held = a;
}

bool tableLookup::Triggered(Flt value) {
  if (std::isnan(value)) value = 0.f;
  const bool fired =
      triggerOn == triggerMode::RISING ? (lastInput == 0.f && value != 0.f) : value != lastInput;
  lastInput = value;
  return fired;
}

void tableLookup::Step(const table& t, Dbl by) {
  Dbl from = cursor;
  if (cursorIsInput) from = MapIn(cursor, t.frames);
  cursorIsInput = false;
  if (gotoWaiting) {
    cursor = gotoIndex;
    gotoWaiting = false;
    return;
  }
  cursor = from + by;
  if (t.frames > 0 && std::isfinite(cursor)) cursor = Wrap(cursor, t.frames);
}

void tableLookup::Draw(const table& t, Dbl& total, bool& haveTotal) {
  cursorIsInput = false;
  gotoWaiting = false;
  cursor = 0.0;
  if (t.samples == nullptr) return;
  if (!haveTotal) {
    total = 0.0;
    for (UInt i = 0; i < t.frames; ++i) {
      const Flt v = detail::LoadSample(t.samples + i);
      if (v > 0.f) total += v; // not a number is never above 0
    }
    haveTotal = true;
  }
  if (!(total > 0.0)) return;
  const Dbl target = (detail::NextWhite(state) + 1.0) * 0.5 * total;
  Dbl sum = 0.0;
  UInt last = 0;
  for (UInt i = 0; i < t.frames; ++i) {
    const Flt v = detail::LoadSample(t.samples + i);
    if (!(v > 0.f)) continue;
    last = i;
    sum += v;
    if (sum > target) {
      cursor = static_cast<Dbl>(i);
      return;
    }
  }
  // Rounding, or a writer on another thread lowered an entry since the
  // total: the last entry that could be chosen.
  cursor = static_cast<Dbl>(last);
}

void tableLookup::set(const sampleView& view, Flt value) {
  if (input == inputMode::LOOKUP) {
    lastInput = value;
    cursor = value;
    cursorIsInput = true;
    gotoWaiting = false;
    return;
  }
  if (!Triggered(value)) return;
  const table t = Table(view);
  if (input == inputMode::INCREMENT) {
    Step(t, 1.0);
  } else {
    Dbl total = 0.0;
    bool haveTotal = false;
    Draw(t, total, haveTotal);
  }
}

void tableLookup::goTo(Flt index) {
  gotoIndex = index;
  gotoWaiting = true;
}

void tableLookup::next(const sampleView& view) {
  Step(Table(view), 1.0);
}

void tableLookup::prev(const sampleView& view) {
  Step(Table(view), -1.0);
}

void tableLookup::draw(const sampleView& view) {
  Dbl total = 0.0;
  bool haveTotal = false;
  Draw(Table(view), total, haveTotal);
}

Dbl tableLookup::position(const sampleView& view) const {
  return cursorIsInput ? MapIn(cursor, Table(view).frames) : cursor;
}

Flt tableLookup::read(const sampleView& view, Dbl index) {
  return ScaleOut(Read(Table(view), index));
}

buffer& tableLookup::operator()(const sampleView& view, buffer& in) {
  const UInt length = BlockLength(in.getLength());
  // Within the capacity set in the constructor: no allocation.
  if (output.getLength() != length) output.resize(length);
  Flt* out = output.getPtr();
  const Flt* x = in.getPtr();
  const table t = Table(view);

  if (input == inputMode::LOOKUP) {
    for (UInt i = 0; i < length; ++i)
      out[i] = ScaleOut(Read(t, MapIn(x[i], t.frames)));
    if (length > 0) lastInput = x[length - 1];
    return output;
  }

  Dbl total = 0.0;
  bool haveTotal = false; // one sum per block, at the first draw
  for (UInt i = 0; i < length; ++i) {
    if (Triggered(x[i])) {
      if (input == inputMode::INCREMENT) {
        Step(t, 1.0);
      } else {
        Draw(t, total, haveTotal);
      }
    }
    out[i] = ScaleOut(Read(t, cursor));
  }
  return output;
}

buffer& tableLookup::operator()(const sampleView& view, UInt length) {
  length = BlockLength(length);
  if (output.getLength() != length) output.resize(length);
  const table t = Table(view);
  const Dbl at = cursorIsInput ? MapIn(cursor, t.frames) : cursor;
  // HOLD's value can only change at the first sample: one read fills the
  // block.
  std::fill_n(output.getPtr(), length, ScaleOut(Read(t, at)));
  return output;
}
