#include "crossover.hpp"

#include <algorithm>
#include <cmath>

namespace {
  constexpr Dbl PI = 3.14159265358979323846;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    crossover::crossover() : low(STANDARD_BUFFERSIZE), high(STANDARD_BUFFERSIZE) {}

    crossover& crossover::cutoff(Flt hz) {
      if (std::isfinite(hz)) freq = hz;
      return *this;
    }

    crossover& crossover::clear() {
      low1 = state();
      low2 = state();
      high1 = state();
      high2 = state();
      last = outputs();
      return *this;
    }

    crossover::coefficients crossover::Coefficients(Flt hz, Flt sampleRate) {
      Dbl k = 0.0;
      if (sampleRate > 0.f && std::isfinite(hz)) {
        const Dbl sr = static_cast<Dbl>(sampleRate);
        const Dbl f = std::clamp(static_cast<Dbl>(hz), 0.0, sr * MAX_CUTOFF_RATIO);
        k = std::tan(PI * f / sr);
      }
      coefficients out;
      const Dbl k2 = k * k;

      // First order: (K + 1) + (K - 1) z^-1.
      const Dbl d1 = k + 1.0;
      out.low1.b0 = k / d1;
      out.low1.b1 = k / d1;
      out.low1.a1 = (k - 1.0) / d1;
      out.high1.b0 = 1.0 / d1;
      out.high1.b1 = -1.0 / d1;
      out.high1.a1 = out.low1.a1;

      // Second order: (1 + K + K^2) + 2 (K^2 - 1) z^-1 + (1 - K + K^2) z^-2.
      const Dbl d2 = 1.0 + k + k2;
      out.low2.b0 = k2 / d2;
      out.low2.b1 = 2.0 * k2 / d2;
      out.low2.b2 = k2 / d2;
      out.low2.a1 = 2.0 * (k2 - 1.0) / d2;
      out.low2.a2 = (1.0 - k + k2) / d2;
      out.high2.b0 = 1.0 / d2;
      out.high2.b1 = -2.0 / d2;
      out.high2.b2 = 1.0 / d2;
      out.high2.a1 = out.low2.a1;
      out.high2.a2 = out.low2.a2;
      return out;
    }

    void crossover::Update() {
      if (cachedRate != SAMPLERATE || cachedFreq != freq) {
        c = Coefficients(freq, static_cast<Flt>(SAMPLERATE));
        cachedRate = SAMPLERATE;
        cachedFreq = freq;
        // At K = 0 the highpass poles sit on z = 1 and would keep whatever
        // offset the state holds: let it go instead.
        if (c.low1.b0 == 0.0) clear();
      }
    }

    crossover::coefficients crossover::coefficient() {
      Update();
      return c;
    }

    Dbl crossover::Run(const section& k, state& s, Dbl x) {
      const Dbl y = k.b0 * x + s.s1;
      s.s1 = k.b1 * x - k.a1 * y + s.s2;
      s.s2 = k.b2 * x - k.a2 * y;
      return y;
    }

    void crossover::Step(Flt in) {
      if (!std::isfinite(in)) return;
      const Dbl x = static_cast<Dbl>(in);
      last.lowpass = static_cast<Flt>(Run(c.low2, low2, Run(c.low1, low1, x)));
      last.highpass = static_cast<Flt>(Run(c.high2, high2, Run(c.high1, high1, x)));
    }

    crossover::outputs crossover::process(Flt in) {
      Update();
      Step(in);
      return last;
    }

    // Within the capacity set in the constructor: no allocation.
    void crossover::Fit(UInt length) {
      if (low.getLength() != length) low.resize(length);
      if (high.getLength() != length) high.resize(length);
    }

    buffer& crossover::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(length);
      Update();
      const Flt* x = in.getPtr();
      Flt* lp = low.getPtr();
      Flt* hp = high.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Step(x[i]);
        lp[i] = last.lowpass;
        hp[i] = last.highpass;
      }
      return low;
    }

    buffer& crossover::operator()(buffer& in, buffer& cutoffSignal) {
      const UInt length = std::min(Clamp(in.getLength()), cutoffSignal.getLength());
      Fit(length);
      const Flt* x = in.getPtr();
      const Flt* f = cutoffSignal.getPtr();
      Flt* lp = low.getPtr();
      Flt* hp = high.getPtr();
      for (UInt i = 0; i < length; ++i) {
        cutoff(f[i]);
        Update();
        Step(x[i]);
        lp[i] = last.lowpass;
        hp[i] = last.highpass;
      }
      return low;
    }

  } // namespace DSP
} // namespace YSE
