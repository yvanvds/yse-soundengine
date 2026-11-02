#include "minMax.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Not 0 and not NaN: NaN compares false both ways.
  inline bool Resets(Flt r) {
    return r > 0.f || r < 0.f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    minMax::minMax()
      : low(0.f),
        high(0.f),
        none(true),
        lowOut(STANDARD_BUFFERSIZE),
        highOut(STANDARD_BUFFERSIZE) {}

    void minMax::track(Flt in) {
      if (std::isnan(in)) return;
      if (none) {
        low = in;
        high = in;
        none = false;
        return;
      }
      if (in < low) low = in;
      if (in > high) high = in;
    }

    void minMax::restart(Flt in) {
      low = 0.f;
      high = 0.f;
      none = true;
      track(in);
    }

    void minMax::process(Flt in) {
      track(in);
    }

    void minMax::process(Flt in, Flt reset) {
      if (Resets(reset))
        restart(in);
      else
        track(in);
    }

    void minMax::fill(UInt length, const Flt* in, const Flt* reset, UInt resetLength) {
      // Within the capacity set in the constructor: no allocation.
      if (lowOut.getLength() != length) lowOut.resize(length);
      if (highOut.getLength() != length) highOut.resize(length);
      Flt* lo = lowOut.getPtr();
      Flt* hi = highOut.getPtr();
      for (UInt i = 0; i < length; ++i) {
        if (i < resetLength && Resets(reset[i]))
          restart(in[i]);
        else
          track(in[i]);
        lo[i] = low;
        hi[i] = high;
      }
    }

    void minMax::operator()(buffer& in) {
      fill(Clamp(in.getLength()), in.getPtr(), nullptr, 0);
    }

    void minMax::operator()(buffer& in, buffer& reset) {
      fill(Clamp(in.getLength()), in.getPtr(), reset.getPtr(), reset.getLength());
    }

    buffer& minMax::minimumSignal() {
      return lowOut;
    }

    buffer& minMax::maximumSignal() {
      return highOut;
    }

    Flt minMax::minimum() const {
      return low;
    }

    Flt minMax::maximum() const {
      return high;
    }

    bool minMax::empty() const {
      return none;
    }

    minMax& minMax::reset() {
      low = 0.f;
      high = 0.f;
      none = true;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
