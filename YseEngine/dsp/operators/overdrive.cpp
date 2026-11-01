#include "overdrive.hpp"

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

  inline Flt Shape(Flt in, Flt drive) {
    const Flt magnitude = std::fabs(in);
    // At or beyond full scale, and NaN (which fails the comparison).
    if (!(magnitude < 1.f)) return in > 0.f ? 1.f : (in < 0.f ? -1.f : 0.f);
    // Linear: exact, where log1p and expm1 would round.
    if (drive == 1.f) return in;
    // 1 - (1 - |x|)^d, without the cancellation of 1 - pow() for small |x|.
    // A negative drive can give any negative value, even -inf near 1.
    const Flt y = std::max(-std::expm1(drive * std::log1p(-magnitude)), -1.f);
    return in < 0.f ? -y : y;
  }
} // namespace

namespace YSE {
  namespace DSP {

    overdrive::overdrive() : output(STANDARD_BUFFERSIZE) {}

    overdrive& overdrive::drive(Flt value) {
      if (std::isfinite(value)) factor = value;
      return *this;
    }

    Flt overdrive::process(Flt in) const {
      return Shape(in, factor);
    }

    buffer& overdrive::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Shape(x[i], factor);
      return output;
    }

    buffer& overdrive::operator()(buffer& in, buffer& driveSignal) {
      const UInt length = std::min(Clamp(in.getLength()), driveSignal.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* d = driveSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        drive(d[i]);
        y[i] = Shape(x[i], factor);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
