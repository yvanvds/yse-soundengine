#include "phasorSwing.hpp"
#include "phaseStep.hpp"

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

  // The double ramp is below 1, but its nearest float can be 1.0f. Keep the
  // output below 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorSwing::phasorSwing() : output(STANDARD_BUFFERSIZE) {}

    phasorSwing& phasorSwing::swing(Flt value) {
      if (!std::isfinite(value)) return *this;
      requested = value < MIN_SWING ? MIN_SWING : (value > MAX_SWING ? MAX_SWING : value);
      return *this;
    }

    phasorSwing& phasorSwing::syncUpdate(bool on) {
      sync = on;
      return *this;
    }

    Flt phasorSwing::Step(Flt in) {
      const Flt x = std::isfinite(in) ? static_cast<Flt>(phaseStep::Wrap(in)) : previous;
      if (!sync || !started || phaseStep::Of(previous, x).wrap != 0) active = requested;
      previous = x;
      started = true;

      const Dbl xd = static_cast<Dbl>(x);
      const Dbl s = static_cast<Dbl>(active);
      return ToSample(xd < s ? xd / s : (xd - s) / (1.0 - s));
    }

    Flt phasorSwing::process(Flt phase) {
      return Step(phase);
    }

    buffer& phasorSwing::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Step(in[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
