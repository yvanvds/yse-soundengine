#include "phaseShift.hpp"

#include <algorithm>
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
} // namespace

namespace YSE {
  namespace DSP {

    phaseShift::phaseShift() : output(STANDARD_BUFFERSIZE) {}

    phaseShift& phaseShift::frequency(Flt hz) {
      if (std::isfinite(hz)) freq = hz;
      return *this;
    }

    phaseShift& phaseShift::q(Flt value) {
      if (std::isfinite(value)) quality = value;
      return *this;
    }

    phaseShift& phaseShift::clear() {
      x1 = 0.0;
      x2 = 0.0;
      y1 = 0.0;
      y2 = 0.0;
      return *this;
    }

    phaseShift::coefficients phaseShift::Coefficients(Flt hz, Flt q, Flt sampleRate) {
      // The cookbook allpass, shared with filterCoefficients: only the
      // feedback is kept, the feedforward is its mirror.
      const filterCoefficients::preciseSet set =
          filterCoefficients::ComputePrecise(filterCoefficients::ALLPASS, hz, 1.f, q, sampleRate);
      coefficients out;
      out.b1 = set.b1;
      out.b2 = set.b2;
      return out;
    }

    void phaseShift::Update() {
      if (valid && cachedRate == SAMPLERATE && cachedFreq == freq && cachedQ == quality) return;
      c = Coefficients(freq, quality, static_cast<Flt>(SAMPLERATE));
      valid = true;
      cachedRate = SAMPLERATE;
      cachedFreq = freq;
      cachedQ = quality;
    }

    phaseShift::coefficients phaseShift::coefficient() {
      Update();
      return c;
    }

    Flt phaseShift::Step(Flt in) {
      if (std::isfinite(in)) {
        // Direct form I of (b2 + b1 z^-1 + z^-2) / (1 + b1 z^-1 + b2 z^-2):
        // the state is the signal itself, so a coefficient that changes per
        // sample leaves no stored products behind.
        const Dbl x = static_cast<Dbl>(in);
        const Dbl y = c.b2 * (x - y2) + c.b1 * (x1 - y1) + x2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        // Overflow: start again from silence rather than latch.
        if (!std::isfinite(static_cast<Flt>(y))) clear();
      }
      return static_cast<Flt>(y1);
    }

    Flt phaseShift::process(Flt in) {
      Update();
      return Step(in);
    }

    buffer& phaseShift::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      Update();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& phaseShift::operator()(buffer& in, buffer& frequencySignal, buffer& qSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), frequencySignal.getLength(), qSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* f = frequencySignal.getPtr();
      const Flt* s = qSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        frequency(f[i]);
        q(s[i]);
        Update();
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
