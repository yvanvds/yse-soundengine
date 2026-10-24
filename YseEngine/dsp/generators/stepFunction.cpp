#include "stepFunction.hpp"
#include "curveBend.hpp"
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

  // A curve from any value: -1..1, 0 when it is not a finite number.
  inline Flt ToCurve(Flt c) {
    if (!std::isfinite(c)) return 0.f;
    if (c < -1.f) return -1.f;
    if (c > 1.f) return 1.f;
    return c;
  }

  // A step signal's value, rounded down, kept in a range that converts.
  inline long long ToIndex(Flt s) {
    const Dbl d = std::floor(static_cast<Dbl>(s));
    if (d > 1.0e9) return 1000000000LL;
    if (d < -1.0e9) return -1000000000LL;
    return static_cast<long long>(d);
  }
} // namespace

namespace YSE {
  namespace DSP {

    stepFunction::stepFunction() : output(STANDARD_BUFFERSIZE) {
      for (UInt i = 0; i < MAX_STEPS; ++i) {
        active.exponents[i] = 1.f;
        requested.exponents[i] = 1.f;
      }
    }

    stepFunction& stepFunction::pattern(const Flt* values, UInt count) {
      requested.count = 0;
      if (values != nullptr) {
        for (UInt i = 0; i < count && requested.count < MAX_STEPS; ++i) {
          if (!std::isfinite(values[i])) continue;
          requested.values[requested.count++] = values[i];
        }
      }
      Request();
      return *this;
    }

    stepFunction& stepFunction::curvePattern(const Flt* curves, UInt count) {
      for (UInt i = 0; i < MAX_STEPS; ++i)
        requested.curves[i] = (curves != nullptr && i < count) ? ToCurve(curves[i]) : 0.f;
      Request();
      return *this;
    }

    stepFunction& stepFunction::breakpoints(const Flt* list, UInt count, bool curves,
                                            const Flt* initial) {
      UInt n = 0;
      if (initial != nullptr && std::isfinite(*initial)) {
        requested.values[n] = *initial;
        if (curves) requested.curves[n] = 0.f;
        ++n;
      }
      const UInt stride = curves ? 3 : 2;
      if (list != nullptr) {
        for (UInt g = 0; g + stride <= count && n < MAX_STEPS; g += stride) {
          if (!std::isfinite(list[g])) continue;
          requested.values[n] = list[g];
          if (curves) requested.curves[n] = ToCurve(list[g + 2]);
          ++n;
        }
      }
      requested.count = n;
      // Triples are the whole curve pattern; pairs carry none and keep it.
      if (curves) {
        for (UInt i = n; i < MAX_STEPS; ++i)
          requested.curves[i] = 0.f;
      }
      Request();
      return *this;
    }

    stepFunction& stepFunction::syncUpdate(SYNC mode) {
      sync = mode;
      if (pending && sync == IMMEDIATE) Land();
      return *this;
    }

    stepFunction& stepFunction::step(int n) {
      const long long steps = active.count == 0 ? 1 : active.count;
      long long k = static_cast<long long>(n) % steps;
      if (k < 0) k += steps;
      current = static_cast<UInt>(k);
      return *this;
    }

    stepFunction& stepFunction::reset() {
      current = 0;
      if (pending) Land();
      return *this;
    }

    void stepFunction::Request() {
      pending = true;
      if (sync == IMMEDIATE || !started) Land();
    }

    void stepFunction::Land() {
      active = requested;
      for (UInt i = 0; i < MAX_STEPS; ++i)
        active.exponents[i] = curveBend::Exponent(active.curves[i]);
      pending = false;
      const UInt steps = active.count == 0 ? 1 : active.count;
      if (current >= steps) current %= steps;
    }

    int stepFunction::Wrap(Flt phase) {
      if (!std::isfinite(phase)) return 0;
      const Flt f = static_cast<Flt>(phaseStep::Wrap(static_cast<Dbl>(phase)));
      int wrap = 0;
      if (started) wrap = phaseStep::Of(previous, f).wrap;
      started = true;
      previous = f;
      return wrap;
    }

    Flt stepFunction::Value(Dbl f) const {
      const UInt n = active.count;
      if (n == 0) return 0.f;
      const Flt to = active.values[current];
      const Flt from = active.values[current == 0 ? n - 1 : current - 1];
      return from + (to - from) * curveBend::Bend(static_cast<Flt>(f), active.exponents[current]);
    }

    Flt stepFunction::Counted(Flt phase) {
      const int wrap = Wrap(phase);
      if (wrap != 0) {
        const UInt steps = active.count == 0 ? 1 : active.count;
        bool around = false;
        if (wrap > 0) {
          around = current + 1 >= steps;
          current = around ? 0 : current + 1;
        } else {
          around = current == 0;
          current = around ? steps - 1 : current - 1;
        }
        if (pending && (sync == INPUT_RESET || (sync == SEQUENCE_END && around))) {
          Land();
          // Round the end backward: the new pattern starts at its last step.
          if (around && wrap < 0) current = active.count == 0 ? 0 : active.count - 1;
        }
      }
      return Value(previous);
    }

    Flt stepFunction::Stepped(Flt phase, Flt stepIn) {
      const bool first = !started;
      const int wrap = Wrap(phase);
      if (std::isfinite(stepIn)) lastStepIn = stepIn;
      const long long index = ToIndex(lastStepIn);
      const bool fell = !first && index < lastIndex;
      lastIndex = index;
      if (pending && ((sync == INPUT_RESET && wrap != 0) || (sync == SEQUENCE_END && fell))) Land();
      const long long steps = active.count == 0 ? 1 : active.count;
      long long k = index % steps;
      if (k < 0) k += steps;
      current = static_cast<UInt>(k);
      return Value(previous);
    }

    Flt stepFunction::process(Flt phase) {
      return Counted(phase);
    }

    Flt stepFunction::process(Flt phase, Flt stepIn) {
      return Stepped(phase, stepIn);
    }

    buffer& stepFunction::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* x = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Counted(x[i]);
      return output;
    }

    buffer& stepFunction::operator()(buffer& phase, buffer& stepIn) {
      UInt length = Clamp(phase.getLength());
      if (stepIn.getLength() < length) length = stepIn.getLength();
      Fit(output, length);
      const Flt* x = phase.getPtr();
      const Flt* s = stepIn.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Stepped(x[i], s[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
