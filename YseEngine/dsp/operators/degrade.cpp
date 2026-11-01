#include "degrade.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  // Bits read from a signal: clamped as a float first, so a huge value
  // cannot overflow the int; a fractional number of bits is truncated.
  inline int BitsFrom(Flt value) {
    const auto lowest = static_cast<Flt>(YSE::DSP::degrade::MIN_BITS);
    const auto highest = static_cast<Flt>(YSE::DSP::degrade::MAX_BITS);
    return static_cast<int>(std::min(std::max(value, lowest), highest));
  }
} // namespace

namespace YSE {
  namespace DSP {

    degrade::degrade() : output(STANDARD_BUFFERSIZE) {}

    degrade& degrade::ratio(Flt value) {
      if (!std::isnan(value)) rate = std::min(std::max(value, 0.f), 1.f);
      return *this;
    }

    degrade& degrade::bits(int value) {
      Depth(std::min(std::max(value, MIN_BITS), MAX_BITS));
      return *this;
    }

    degrade& degrade::reset() {
      phase = 1.0;
      hold = 0.f;
      return *this;
    }

    void degrade::Depth(int value) {
      if (value == depth) return;
      depth = value;
      // Powers of two: the scaling and the step back are exact in float.
      scale = std::ldexp(1.f, value - 1);
      step = std::ldexp(1.f, 1 - value);
    }

    Flt degrade::Step(Flt in) {
      if (phase >= 1.0) {
        phase -= 1.0;
        if (std::isfinite(in)) hold = in;
      }
      phase += rate;
      return std::floor(hold * scale) * step;
    }

    Flt degrade::process(Flt in) {
      return Step(in);
    }

    buffer& degrade::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& degrade::operator()(buffer& in, buffer& ratioSignal, buffer& bitsSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), ratioSignal.getLength(), bitsSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* r = ratioSignal.getPtr();
      const Flt* b = bitsSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        ratio(r[i]);
        if (!std::isnan(b[i])) Depth(BitsFrom(b[i]));
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
