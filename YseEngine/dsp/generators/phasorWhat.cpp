#include "phasorWhat.hpp"
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

  // A step rising from a to b (b > a, b below a + 1) passes phase t, or t + 1
  // past a forward wrap: a <= t < b.
  inline bool Rises(Dbl a, Dbl b, Dbl t) {
    return (a <= t && t < b) || (a <= t + 1.0 && t + 1.0 < b);
  }

  // A step falling from a to b (b < a, b above a - 1) passes phase t, or
  // t - 1 past a backward wrap: b < t <= a.
  inline bool Falls(Dbl a, Dbl b, Dbl t) {
    return (b < t && t <= a) || (b < t - 1.0 && t - 1.0 <= a);
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorWhat::phasorWhat() : output(STANDARD_BUFFERSIZE) {}

    phasorWhat& phasorWhat::matches(const Flt* values, UInt count) {
      requested.count = 0;
      if (values != nullptr) {
        for (UInt i = 0; i < count && requested.count < MAX_MATCHES; ++i) {
          if (!std::isfinite(values[i])) continue;
          requested.values[requested.count++] =
              static_cast<Flt>(phaseStep::Wrap(static_cast<Dbl>(values[i])));
        }
      }
      pending = true;
      if (!synced) Land();
      return *this;
    }

    phasorWhat& phasorWhat::triggerMode(TRIGGER_MODE m) {
      mode = m;
      return *this;
    }

    phasorWhat& phasorWhat::syncUpdate(bool on) {
      synced = on;
      return *this;
    }

    void phasorWhat::Land() {
      active = requested;
      pending = false;
    }

    Flt phasorWhat::Step(Flt in) {
      if (!std::isfinite(in)) return 0.f;
      const Flt x = static_cast<Flt>(phaseStep::Wrap(static_cast<Dbl>(in)));
      const bool first = !started;
      phaseStep::step s{0.0, 0};
      if (started) s = phaseStep::Of(previous, x);
      if (pending && (!synced || s.wrap != 0 || first)) Land();

      const Dbl a = static_cast<Dbl>(previous);
      previous = x;
      started = true;

      if (mode == EQUALS) {
        for (UInt i = 0; i < active.count; ++i)
          if (x == active.values[i]) return 1.f;
        return 0.f;
      }
      if (first || s.delta == 0.0) return 0.f;

      const Dbl b = a + s.delta;
      if (s.delta > 0.0) {
        if (mode == DESCENDING) return 0.f;
        for (UInt i = 0; i < active.count; ++i)
          if (Rises(a, b, static_cast<Dbl>(active.values[i]))) return 1.f;
      } else {
        if (mode == ASCENDING) return 0.f;
        for (UInt i = 0; i < active.count; ++i)
          if (Falls(a, b, static_cast<Dbl>(active.values[i]))) return 1.f;
      }
      return 0.f;
    }

    Flt phasorWhat::process(Flt phase) {
      return Step(phase);
    }

    buffer& phasorWhat::operator()(buffer& phase) {
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
