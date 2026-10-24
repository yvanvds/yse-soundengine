#include "stepCounter.hpp"

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

  // A count from a finite value: rounded, at least 1, at most a billion.
  inline int ToCount(Flt value) {
    const Dbl rounded = std::floor(static_cast<Dbl>(value) + 0.5);
    if (rounded < 1.0) return 1;
    if (rounded > 1.0e9) return 1000000000;
    return static_cast<int>(rounded);
  }
} // namespace

namespace YSE {
  namespace DSP {

    stepCounter::stepCounter()
      : output(STANDARD_BUFFERSIZE),
        countOutput(STANDARD_BUFFERSIZE),
        stepOutput(STANDARD_BUFFERSIZE) {}

    stepCounter& stepCounter::sequence(const Flt* values, UInt count) {
      requested.count = 0;
      if (values != nullptr) {
        for (UInt i = 0; i < count && requested.count < MAX_STEPS; ++i) {
          if (!std::isfinite(values[i])) continue;
          requested.values[requested.count++] = ToCount(values[i]);
        }
      }
      pending = true;
      if (sync == IMMEDIATE || !started) Land();
      return *this;
    }

    stepCounter& stepCounter::direction(DIRECTION d) {
      dir = d;
      return *this;
    }

    stepCounter& stepCounter::startMode(bool on) {
      startImpulse = on;
      return *this;
    }

    stepCounter& stepCounter::syncUpdate(SYNC mode) {
      sync = mode;
      if (pending && sync == IMMEDIATE) Land();
      return *this;
    }

    stepCounter& stepCounter::threshold(Flt value) {
      if (std::isfinite(value)) thresh = value;
      return *this;
    }

    stepCounter& stepCounter::reset() {
      counted = 0;
      current = 0;
      started = false;
      if (pending) Land();
      return *this;
    }

    void stepCounter::Land() {
      active = requested;
      pending = false;
      const UInt steps = active.count == 0 ? 1 : active.count;
      if (current >= steps) current %= steps;
    }

    int stepCounter::Target() const {
      return active.count == 0 ? 1 : active.values[current];
    }

    Flt stepCounter::Step(Flt in) {
      if (!std::isfinite(in)) return 0.f;
      const Flt d = in - previous;
      previous = in;

      const Flt size = std::fabs(d);
      const bool jump = d != 0.f && size >= thresh &&
                        (dir == BOTH || (dir == UP && d > 0.f) || (dir == DOWN && d < 0.f));
      if (!jump) return 0.f;

      if (!started) {
        started = true;
        // The first jump starts the sequence, with an impulse.
        if (startImpulse) return 1.f;
      }

      if (++counted < Target()) return 0.f;
      counted = 0;
      const UInt steps = active.count == 0 ? 1 : active.count;
      ++current;
      const bool wrapped = current >= steps;
      if (wrapped) current = 0;
      if (pending && (sync == AFTER_STEP || (sync == AFTER_SEQUENCE && wrapped))) Land();
      return 1.f;
    }

    Flt stepCounter::process(Flt in) {
      return Step(in);
    }

    buffer& stepCounter::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      Fit(countOutput, length);
      Fit(stepOutput, length);
      const Flt* x = in.getPtr();
      Flt* out = output.getPtr();
      Flt* count = countOutput.getPtr();
      Flt* step = stepOutput.getPtr();
      for (UInt i = 0; i < length; ++i) {
        out[i] = Step(x[i]);
        count[i] = static_cast<Flt>(counted);
        step[i] = static_cast<Flt>(current);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
