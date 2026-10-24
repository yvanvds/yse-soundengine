#include "phasorRate.hpp"
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

  // The double phase is below 1, but the nearest float to 0.99999999 is 1.0f.
  // Keep the output below 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  inline bool Usable(Flt multiplier) {
    return std::isfinite(multiplier) && multiplier != 0.f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorRate::phasorRate() : output(STANDARD_BUFFERSIZE) {}

    phasorRate& phasorRate::multiplier(Flt value) {
      if (Usable(value)) rate = value;
      return *this;
    }

    phasorRate& phasorRate::sync(syncMode value) {
      mode = value;
      return *this;
    }

    phasorRate& phasorRate::oneshot(bool on) {
      single = on;
      if (!on) ended = false;
      return *this;
    }

    phasorRate& phasorRate::jump(Flt value) {
      if (!std::isfinite(value)) return *this;
      jumpPending = NOW;
      jumpValue = value;
      return *this;
    }

    phasorRate& phasorRate::jump(Flt value, Flt at) {
      if (!std::isfinite(at)) return jump(value);
      if (!std::isfinite(value)) return *this;
      jumpPending = AT;
      jumpValue = value;
      jumpAt = at < 0.f ? 0.f : (at > 1.f ? 1.f : at);
      return *this;
    }

    phasorRate& phasorRate::reset() {
      resetPending = true;
      return *this;
    }

    Flt phasorRate::Step(Flt in, Flt m) {
      if (!std::isfinite(in)) in = previous;
      if (!Usable(m)) m = current;
      const bool changed = m != current;
      current = m;

      const phaseStep::step s = phaseStep::Of(previous, in);
      const bool fire = jumpPending == NOW ||
                        (jumpPending == AT && phaseStep::Passes(previous, in, s.wrap, jumpAt));
      previous = in;
      wraps += s.wrap;

      const Dbl md = static_cast<Dbl>(m);
      if (resetPending && s.wrap != 0) {
        resetPending = false;
        wraps = 0.0;
        ended = false;
        position = phaseStep::Wrap(static_cast<Dbl>(in) / md);
      } else if (!ended) {
        Dbl next = position + s.delta / md;
        if (next >= 1.0 || next < 0.0) {
          if (single) {
            ended = true;
            endValue = next >= 1.0 ? 1.f : 0.f;
            next = endValue;
          } else {
            next = phaseStep::Wrap(next);
          }
        }
        position = next;
        if (!ended &&
            ((mode == syncMode::lock && changed) || (mode == syncMode::cycle && s.wrap != 0)))
          position = phaseStep::Wrap((wraps + static_cast<Dbl>(in)) / md);
      }

      if (fire) {
        jumpPending = NONE;
        ended = false;
        position = jumpValue;
        return jumpValue;
      }
      return ended ? endValue : ToSample(position);
    }

    Flt phasorRate::process(Flt phase) {
      return Step(phase, rate);
    }

    Flt phasorRate::process(Flt phase, Flt multiplier) {
      return Step(phase, multiplier);
    }

    buffer& phasorRate::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Step(in[i], rate);
      return output;
    }

    buffer& phasorRate::operator()(buffer& phase, buffer& multiplier) {
      const UInt length = Clamp(
          phase.getLength() < multiplier.getLength() ? phase.getLength() : multiplier.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      const Flt* m = multiplier.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Step(in[i], m[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
