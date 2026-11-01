#include "normalize.hpp"

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
} // namespace

namespace YSE {
  namespace DSP {

    normalize::normalize() : output(STANDARD_BUFFERSIZE) {}

    normalize& normalize::amplitude(Flt value) {
      if (std::isfinite(value)) target = value;
      return *this;
    }

    normalize& normalize::peak(Flt value) {
      if (std::isfinite(value)) top = std::max(std::fabs(value), MIN_PEAK);
      return *this;
    }

    normalize& normalize::reset() {
      top = MIN_PEAK;
      return *this;
    }

    Flt normalize::Step(Flt in) {
      if (!std::isfinite(in)) return 0.f;
      const Flt magnitude = std::fabs(in);
      if (magnitude > top) top = magnitude;
      // Divided first: |in / top| <= 1, so a tiny peak cannot overflow.
      return in / top * target;
    }

    Flt normalize::process(Flt in) {
      return Step(in);
    }

    buffer& normalize::operator()(buffer& in) {
      const UInt size = Clamp(in.getLength());
      Fit(output, size);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < size; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& normalize::operator()(buffer& in, buffer& amplitudeSignal) {
      const UInt size = std::min(Clamp(in.getLength()), amplitudeSignal.getLength());
      Fit(output, size);
      const Flt* x = in.getPtr();
      const Flt* a = amplitudeSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < size; ++i) {
        amplitude(a[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
