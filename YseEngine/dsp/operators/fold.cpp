#include "fold.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;
  using MODE = YSE::DSP::fold::MODE;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  // Carries a value around to the other side of [lo, hi). std::fmod keeps the
  // sign of its left operand, so a value below the range comes back negative
  // and is lifted by one span.
  inline Flt Wrap(Flt value, Flt lo, Flt span) {
    Flt offset = std::fmod(value - lo, span);
    if (offset < 0.f) offset += span;
    return lo + offset;
  }

  // Reflects a value back into [lo, hi]: periodic over twice the span, a
  // triangle wave, so a value far outside folds as often as it takes. Both
  // limits are fixed points, the difference from Wrap().
  inline Flt Reflect(Flt value, Flt lo, Flt span) {
    const Flt period = span * 2.f;
    Flt offset = std::fmod(value - lo, period);
    if (offset < 0.f) offset += period;
    // Past the top of the range the second half of the period runs back down.
    if (offset > span) offset = period - offset;
    return lo + offset;
  }
} // namespace

namespace YSE {
  namespace DSP {

    fold::fold() : output(STANDARD_BUFFERSIZE) {}

    fold& fold::mode(MODE value) {
      how = value;
      return *this;
    }

    fold& fold::low(Flt value) {
      if (std::isfinite(value)) lowLimit = value;
      return *this;
    }

    fold& fold::high(Flt value) {
      if (std::isfinite(value)) highLimit = value;
      return *this;
    }

    fold& fold::range(Flt lowValue, Flt highValue) {
      return low(lowValue).high(highValue);
    }

    Flt fold::Limit(Flt value, Flt lowValue, Flt highValue, MODE how) {
      // The limits may arrive the wrong way round.
      const Flt lo = std::min(lowValue, highValue);
      const Flt hi = std::max(lowValue, highValue);

      if (std::isnan(value)) value = 0.f;
      // Inside the range: unchanged, bit for bit. The top of the range wraps.
      if (value >= lo && (value < hi || (value == hi && how != MODE::WRAP))) return value;

      const Flt span = hi - lo;
      // One legal answer, and no fmod() by zero below.
      if (span == 0.f) return lo;
      // fmod() of an infinity is NaN: pin it to the limit it ran past.
      if (std::isinf(value)) return value > 0.f ? hi : lo;

      Flt result;
      switch (how) {
      case MODE::CLIP:
        result = value < lo ? lo : hi;
        break;
      case MODE::WRAP:
        result = Wrap(value, lo, span);
        break;
      case MODE::FOLD:
      default:
        result = Reflect(value, lo, span);
        break;
      }
      // Rounding in fmod() can leave a value many spans away a hair outside.
      return std::min(std::max(result, lo), hi);
    }

    Flt fold::process(Flt in) const {
      return Limit(in, lowLimit, highLimit, how);
    }

    buffer& fold::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Limit(x[i], lowLimit, highLimit, how);
      return output;
    }

    buffer& fold::operator()(buffer& in, buffer& lowSignal, buffer& highSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), lowSignal.getLength(), highSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* lo = lowSignal.getPtr();
      const Flt* hi = highSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        range(lo[i], hi[i]);
        y[i] = Limit(x[i], lowLimit, highLimit, how);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
