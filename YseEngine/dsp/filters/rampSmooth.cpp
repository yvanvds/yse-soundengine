#include "rampSmooth.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;

  // The longest ramp: 2^31 samples, over half a day at 48 kHz. Exact in a
  // float, and keeps the cast in range.
  constexpr Flt LONGEST_RAMP = 2147483648.f;

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

    rampSmooth::rampSmooth() : output(STANDARD_BUFFERSIZE) {}

    rampSmooth& rampSmooth::up(Flt samples) {
      if (std::isfinite(samples)) rampUp = samples;
      return *this;
    }

    rampSmooth& rampSmooth::down(Flt samples) {
      if (std::isfinite(samples)) rampDown = samples;
      return *this;
    }

    rampSmooth& rampSmooth::ramp(Flt samples) {
      return up(samples).down(samples);
    }

    rampSmooth& rampSmooth::reset() {
      current = 0.0;
      increment = 0.0;
      target = 0.f;
      remaining = 0;
      return *this;
    }

    UInt rampSmooth::Samples(Flt samples) {
      // A NaN fails the comparison and reads as 1 too.
      if (!(samples >= 1.f)) return 1;
      if (samples >= LONGEST_RAMP) return static_cast<UInt>(LONGEST_RAMP);
      return static_cast<UInt>(samples);
    }

    Flt rampSmooth::Step(Flt in) {
      if (std::isfinite(in) && in != target) {
        // A new value: a ramp from where the output is now.
        target = in;
        const Dbl goal = static_cast<Dbl>(in);
        const UInt length = Samples(goal > current ? rampUp : rampDown);
        increment = (goal - current) / static_cast<Dbl>(length);
        remaining = length;
      }
      if (remaining > 0) {
        remaining--;
        // The last sample is the target itself: no rounding left over.
        current = remaining == 0 ? static_cast<Dbl>(target) : current + increment;
      }
      return static_cast<Flt>(current);
    }

    Flt rampSmooth::process(Flt in) {
      return Step(in);
    }

    buffer& rampSmooth::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& rampSmooth::operator()(buffer& in, buffer& upSignal, buffer& downSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), upSignal.getLength(), downSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* u = upSignal.getPtr();
      const Flt* d = downSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        up(u[i]);
        down(d[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
