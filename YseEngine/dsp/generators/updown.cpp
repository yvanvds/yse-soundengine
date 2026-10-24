#include "updown.hpp"

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

  // A time in milliseconds as samples at the live rate.
  inline Flt Samples(Flt ms) {
    return ms * static_cast<Flt>(YSE::SAMPLERATE) * 0.001f;
  }

  // The largest step per sample for an edge of `samples`: a whole step
  // (instant) for an edge shorter than one sample.
  inline Flt Step(Flt samples) {
    return samples > 1.f ? 1.f / samples : 1.f;
  }

  // How many samples after this one are left before the ramp reaches its
  // end: 1 for a rising ramp, 0 for a falling one. A ramp with no slope (a
  // gate) never ends.
  inline Flt Remaining(Flt x, Flt slope) {
    Flt left = 0.f;
    if (slope > 0.f) {
      left = (1.f - x) / slope - 1.f;
    } else if (slope < 0.f) {
      left = x / -slope - 1.f;
    } else {
      return INFINITY;
    }
    return left > 0.f ? left : 0.f;
  }

  // The fall's target: 1 until the last `downSamples` before the end, then a
  // straight line to 0 at the end.
  inline Flt Target(Flt remaining, Flt downSamples) {
    if (downSamples <= 0.f) return remaining > 0.f ? 1.f : 0.f;
    if (remaining >= downSamples) return 1.f;
    return remaining / downSamples;
  }

  inline Flt NonNegative(Flt value) {
    return value < 0.f ? 0.f : value;
  }
} // namespace

namespace YSE {
  namespace DSP {

    updown::updown() : output(STANDARD_BUFFERSIZE) {}

    updown& updown::up(Flt ms) {
      if (std::isfinite(ms)) upTime = NonNegative(ms);
      return *this;
    }

    updown& updown::down(Flt ms) {
      if (std::isfinite(ms)) downTime = NonNegative(ms);
      return *this;
    }

    updown& updown::level(Flt value) {
      if (std::isfinite(value)) top = value;
      return *this;
    }

    updown& updown::resetThreshold(Flt value) {
      if (std::isfinite(value)) threshold = NonNegative(value);
      return *this;
    }

    updown& updown::restart() {
      env = 0.f;
      return *this;
    }

    buffer& updown::operator()(buffer& input) {
      const UInt length = Clamp(input.getLength());
      Fit(output, length);
      // The live rate, once per block (#634).
      const Flt downSamples = Samples(downTime);
      const Flt rise = Step(Samples(upTime));
      const Flt fall = Step(downSamples);
      const Flt* in = input.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Flt x = in[i];
        if (!std::isfinite(x)) x = 0.f;

        Flt target = 0.f;
        if (x != 0.f) {
          const Flt jump = x - previous;
          // A reset keeps the old slope: the jump is not the ramp's speed.
          const bool reset = previous == 0.f || std::fabs(jump) > threshold;
          if (!reset) slope = jump;
          target = Target(Remaining(x, slope), downSamples);
        }
        previous = x;

        if (env < target) {
          env = env + rise < target ? env + rise : target;
        } else if (env > target) {
          env = env - fall > target ? env - fall : target;
        }
        out[i] = top * env;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
