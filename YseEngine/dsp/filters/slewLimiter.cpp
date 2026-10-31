#include "slewLimiter.hpp"

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

    slewLimiter::slewLimiter() : output(STANDARD_BUFFERSIZE) {}

    slewLimiter& slewLimiter::minimum(Flt slope) {
      if (!std::isnan(slope)) lowest = slope;
      return *this;
    }

    slewLimiter& slewLimiter::maximum(Flt slope) {
      if (!std::isnan(slope)) highest = slope;
      return *this;
    }

    slewLimiter& slewLimiter::set(Flt value) {
      if (std::isfinite(value)) current = value;
      return *this;
    }

    slewLimiter& slewLimiter::reset() {
      current = 0.f;
      return *this;
    }

    Flt slewLimiter::Step(Flt in) {
      if (std::isfinite(in)) {
        const Flt change = in - current;
        if (change >= lowest && change <= highest)
          current = in; // within the limits: the input exactly
        else
          // Clipped as clip does: a minimum above the maximum gives the
          // maximum.
          current += std::min(std::max(change, lowest), highest);
      }
      return current;
    }

    Flt slewLimiter::process(Flt in) {
      return Step(in);
    }

    buffer& slewLimiter::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& slewLimiter::operator()(buffer& in, buffer& minimumSignal, buffer& maximumSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), minimumSignal.getLength(), maximumSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* lo = minimumSignal.getPtr();
      const Flt* hi = maximumSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        minimum(lo[i]);
        maximum(hi[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
