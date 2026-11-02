#include "scale.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;
  using MODE = YSE::DSP::scale::MODE;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline void Store(Flt& target, Flt value) {
    if (std::isfinite(value)) target = value;
  }

  // The modern power curve (lifted from .scale's MapRange, #443).
  inline Flt Power(Flt value, Flt inLow, Flt inHigh, Flt outLow, Flt outHigh, Flt exponent) {
    const Flt span = inHigh - inLow;
    // A collapsed input range has no mapping to give: every input would
    // divide by zero and come out as an infinity or a NaN.
    if (span == 0.f) return outLow;
    const Flt normalized = (value - inLow) / span;
    Flt curved;
    if (exponent == 1.f) {
      // The common case, and std::pow(x, 1) is not guaranteed to be exact.
      curved = normalized;
    } else if (normalized >= 0.f) {
      curved = std::pow(normalized, exponent);
    } else {
      // Mirrored below the input range rather than handing a negative base
      // to pow (which has no real result for a fractional exponent).
      curved = -std::pow(-normalized, exponent);
    }
    return outLow + (outHigh - outLow) * curved;
  }

  // MSP's legacy "linedrive" formula, term for term as its reference prints
  // it: the base is the exponent, the input is not normalized.
  inline Flt Linedrive(Flt value, Flt inLow, Flt inHigh, Flt outLow, Flt outHigh, Flt base) {
    const Flt range = outHigh - outLow;
    const Flt logBase = std::log(base);
    const Flt result = outLow + range * (range * std::exp(-(inHigh - inLow) * logBase) *
                                         std::exp(value * logBase));
    return range >= 0.f ? result : -result;
  }
} // namespace

namespace YSE {
  namespace DSP {

    scale::scale() : output(STANDARD_BUFFERSIZE) {}

    scale& scale::mode(MODE value) {
      how = value;
      return *this;
    }

    scale& scale::inLow(Flt value) {
      Store(inLowValue, value);
      return *this;
    }

    scale& scale::inHigh(Flt value) {
      Store(inHighValue, value);
      return *this;
    }

    scale& scale::outLow(Flt value) {
      Store(outLowValue, value);
      return *this;
    }

    scale& scale::outHigh(Flt value) {
      Store(outHighValue, value);
      return *this;
    }

    scale& scale::exponent(Flt value) {
      Store(exponentValue, value);
      return *this;
    }

    scale& scale::inRange(Flt lowValue, Flt highValue) {
      return inLow(lowValue).inHigh(highValue);
    }

    scale& scale::outRange(Flt lowValue, Flt highValue) {
      return outLow(lowValue).outHigh(highValue);
    }

    Flt scale::Map(Flt value, Flt inLowValue, Flt inHighValue, Flt outLowValue, Flt outHighValue,
                   Flt exponentValue, MODE how) {
      Flt result;
      switch (how) {
      case MODE::CLASSIC:
        result =
            Linedrive(value, inLowValue, inHighValue, outLowValue, outHighValue, exponentValue);
        break;
      case MODE::MODERN_INVERSE:
        if (outLowValue > outHighValue) exponentValue = 1.f / exponentValue;
        result = Power(value, inLowValue, inHighValue, outLowValue, outHighValue, exponentValue);
        break;
      case MODE::MODERN:
      default:
        result = Power(value, inLowValue, inHighValue, outLowValue, outHighValue, exponentValue);
        break;
      }
      // A NaN input, an overflow, or an exponent at or below 0: a NaN or an
      // infinity must not poison everything downstream.
      return std::isfinite(result) ? result : 0.f;
    }

    Flt scale::process(Flt in) const {
      return Map(in, inLowValue, inHighValue, outLowValue, outHighValue, exponentValue, how);
    }

    buffer& scale::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Map(x[i], inLowValue, inHighValue, outLowValue, outHighValue, exponentValue, how);
      return output;
    }

    buffer& scale::operator()(buffer& in, buffer& inLowSignal, buffer& inHighSignal,
                              buffer& outLowSignal, buffer& outHighSignal, buffer& exponentSignal) {
      const UInt length = std::min({Clamp(in.getLength()), inLowSignal.getLength(),
                                    inHighSignal.getLength(), outLowSignal.getLength(),
                                    outHighSignal.getLength(), exponentSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* il = inLowSignal.getPtr();
      const Flt* ih = inHighSignal.getPtr();
      const Flt* ol = outLowSignal.getPtr();
      const Flt* oh = outHighSignal.getPtr();
      const Flt* ex = exponentSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Store(inLowValue, il[i]);
        Store(inHighValue, ih[i]);
        Store(outLowValue, ol[i]);
        Store(outHighValue, oh[i]);
        Store(exponentValue, ex[i]);
        y[i] = Map(x[i], inLowValue, inHighValue, outLowValue, outHighValue, exponentValue, how);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
