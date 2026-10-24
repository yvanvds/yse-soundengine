#include "phasorTwist.hpp"
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

  // Keep the output in 0 .. 1 and below 1, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    if (p <= 0.0) return 0.f;
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorTwist::phasorTwist() : output(STANDARD_BUFFERSIZE) {}

    phasorTwist& phasorTwist::curve(Flt value) {
      if (!std::isfinite(value)) return *this;
      requested = value < -1.f ? -1.f : (value > 1.f ? 1.f : value);
      return *this;
    }

    phasorTwist& phasorTwist::shapeMode(bool on) {
      symmetric = on;
      return *this;
    }

    phasorTwist& phasorTwist::syncUpdate(bool on) {
      sync = on;
      return *this;
    }

    void phasorTwist::Land() {
      if (active == requested) return;
      active = requested;
      if (active == 0.f) {
        steepness = 0.0;
        span = 0.0;
        return;
      }
      const Dbl h = 0.91 * std::pow(1.2 * std::fabs(static_cast<Dbl>(active)), 0.41);
      steepness = h / (1.0 - h);
      span = std::expm1(steepness);
    }

    Dbl phasorTwist::Bend(Dbl x) const {
      if (active == 0.f) return x;
      if (active > 0.f) return std::expm1(steepness * x) / span;
      return 1.0 - std::expm1(steepness * (1.0 - x)) / span;
    }

    Flt phasorTwist::Step(Flt in) {
      const Flt x = std::isfinite(in) ? static_cast<Flt>(phaseStep::Wrap(in)) : previous;
      bool restart = !started;
      if (started) {
        const phaseStep::step s = phaseStep::Of(previous, x);
        const int moved = s.delta > 0.0 ? 1 : (s.delta < 0.0 ? -1 : 0);
        if (s.wrap != 0) restart = true;
        if (moved != 0 && moved != direction) {
          direction = moved;
          restart = true;
        }
      }
      if (!sync || restart) Land();
      previous = x;
      started = true;

      const Dbl xd = static_cast<Dbl>(x);
      if (symmetric && direction < 0) return ToSample(1.0 - Bend(1.0 - xd));
      return ToSample(Bend(xd));
    }

    Flt phasorTwist::process(Flt phase) {
      return Step(phase);
    }

    buffer& phasorTwist::operator()(buffer& phase) {
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
