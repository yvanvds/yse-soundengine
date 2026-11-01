/*
  ==============================================================================

    frequencyShifter.cpp
    A time-domain single-sideband frequency shifter (issue #1110).

  ==============================================================================
*/

#include "frequencyShifter.hpp"

#include <cmath>

namespace {
  constexpr Dbl TWO_PI = 6.283185307179586476925;

  // Wrap into [0, 1). The guard catches rounding onto 1 and anything that is
  // not a finite number, which restarts the phase rather than poisoning it.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return (p >= 0.0 && p < 1.0) ? p : 0.0;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    frequencyShifter::frequencyShifter()
      : upOut(STANDARD_BUFFERSIZE),
        downOut(STANDARD_BUFFERSIZE),
        current(0.0),
        shiftHz(DEFAULT_SHIFT) {}

    frequencyShifter& frequencyShifter::shift(Flt hz) {
      if (std::isfinite(hz)) shiftHz = hz;
      return *this;
    }

    frequencyShifter& frequencyShifter::clear() {
      quadrature.clear();
      return *this;
    }

    frequencyShifter& frequencyShifter::resetPhase(Flt value) {
      current = Wrap(static_cast<Dbl>(value));
      return *this;
    }

    Flt frequencyShifter::phase() const {
      const Flt v = static_cast<Flt>(current);
      return v < 1.0f ? v : 0.99999994f;
    }

    frequencyShifter::outputs frequencyShifter::Step(Flt in, Dbl increment) {
      const hilbert::outputs h = quadrature.process(in);
      const Dbl angle = TWO_PI * current;
      const Dbl re = static_cast<Dbl>(h.real) * std::cos(angle);
      const Dbl im = static_cast<Dbl>(h.imaginary) * std::sin(angle);
      current = Wrap(current + increment);
      outputs o;
      o.up = static_cast<Flt>(re - im);
      o.down = static_cast<Flt>(re + im);
      return o;
    }

    frequencyShifter::outputs frequencyShifter::process(Flt in) {
      return Step(in, static_cast<Dbl>(shiftHz) / static_cast<Dbl>(SAMPLERATE));
    }

    buffer& frequencyShifter::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (upOut.getLength() != length) upOut.resize(length);
      if (downOut.getLength() != length) downOut.resize(length);
      const Dbl increment = static_cast<Dbl>(shiftHz) / static_cast<Dbl>(SAMPLERATE);
      const Flt* x = in.getPtr();
      Flt* u = upOut.getPtr();
      Flt* d = downOut.getPtr();
      for (UInt n = 0; n < length; ++n) {
        const outputs o = Step(x[n], increment);
        u[n] = o.up;
        d[n] = o.down;
      }
      return upOut;
    }

    buffer& frequencyShifter::operator()(buffer& in, buffer& shift) {
      const UInt shorter = in.getLength() < shift.getLength() ? in.getLength() : shift.getLength();
      const UInt length = Clamp(shorter);
      if (upOut.getLength() != length) upOut.resize(length);
      if (downOut.getLength() != length) downOut.resize(length);
      const Dbl perHz = 1.0 / static_cast<Dbl>(SAMPLERATE);
      const Flt* x = in.getPtr();
      const Flt* s = shift.getPtr();
      Flt* u = upOut.getPtr();
      Flt* d = downOut.getPtr();
      for (UInt n = 0; n < length; ++n) {
        if (std::isfinite(s[n])) shiftHz = s[n];
        const outputs o = Step(x[n], static_cast<Dbl>(shiftHz) * perHz);
        u[n] = o.up;
        d[n] = o.down;
      }
      return upOut;
    }

  } // namespace DSP
} // namespace YSE
