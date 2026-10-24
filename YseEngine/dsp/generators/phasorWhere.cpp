#include "phasorWhere.hpp"
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

  // Milliseconds per sample at the live sample rate (0 if it is not set).
  inline Dbl MsPerSample() {
    const Dbl rate = static_cast<Dbl>(YSE::SAMPLERATE);
    return rate > 0.0 ? 1000.0 / rate : 0.0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorWhere::phasorWhere()
      : elapsedOutput(STANDARD_BUFFERSIZE), remainingOutput(STANDARD_BUFFERSIZE) {}

    // The part of the cycle done at input x, in the current direction.
    Dbl phasorWhere::Progress(Flt x) const {
      const Dbl xd = static_cast<Dbl>(x);
      return direction > 0 ? xd : 1.0 - xd;
    }

    void phasorWhere::Step(Flt in, Dbl& elapsedOut, Dbl& remainingOut) {
      const Flt x = std::isfinite(in) ? static_cast<Flt>(phaseStep::Wrap(in)) : previous;
      if (started) {
        const phaseStep::step s = phaseStep::Of(previous, x);
        if (s.delta != 0.0) {
          const int moved = s.delta > 0.0 ? 1 : -1;
          const bool first = speed == 0.0;
          speed = std::fabs(s.delta);
          if (s.wrap != 0 || moved != direction || first) {
            // A new cycle: from the wrap, the reset lies progress / speed
            // samples back; without one, that is the estimate.
            direction = moved;
            elapsed = Progress(x) / speed;
          } else {
            elapsed += 1.0;
          }
        } else if (speed > 0.0) {
          elapsed += 1.0;
        }
      }
      previous = x;
      started = true;

      if (speed == 0.0) {
        elapsedOut = 0.0;
        remainingOut = 0.0;
        return;
      }
      elapsedOut = elapsed;
      remainingOut = (1.0 - Progress(x)) / speed;
    }

    Flt phasorWhere::process(Flt phase) {
      const Dbl ms = MsPerSample();
      Dbl e = 0.0;
      Dbl r = 0.0;
      Step(phase, e, r);
      currentRemaining = static_cast<Flt>(r * ms);
      return static_cast<Flt>(e * ms);
    }

    buffer& phasorWhere::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(elapsedOutput, length);
      Fit(remainingOutput, length);
      const Dbl ms = MsPerSample();
      const Flt* in = phase.getPtr();
      Flt* e = elapsedOutput.getPtr();
      Flt* r = remainingOutput.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Dbl elapsedSamples = 0.0;
        Dbl remainingSamples = 0.0;
        Step(in[i], elapsedSamples, remainingSamples);
        e[i] = static_cast<Flt>(elapsedSamples * ms);
        r[i] = static_cast<Flt>(remainingSamples * ms);
      }
      if (length > 0) currentRemaining = r[length - 1];
      return elapsedOutput;
    }

  } // namespace DSP
} // namespace YSE
