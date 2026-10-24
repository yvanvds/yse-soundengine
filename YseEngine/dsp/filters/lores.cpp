#include "lores.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;

  constexpr Dbl PI = 3.14159265358979323846;
  constexpr Dbl SQRT2 = 1.41421356237309504880;

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

    lores::lores() : output(STANDARD_BUFFERSIZE) {}

    lores& lores::cutoff(Flt hz) {
      if (std::isfinite(hz)) freq = hz;
      return *this;
    }

    lores& lores::resonance(Flt value) {
      if (std::isfinite(value)) res = value;
      return *this;
    }

    lores& lores::clear() {
      y1 = 0.0;
      y2 = 0.0;
      return *this;
    }

    Dbl lores::Q(Flt value) {
      if (!std::isfinite(value)) value = 0.f;
      const Dbl r = std::clamp(static_cast<Dbl>(value), 0.0, static_cast<Dbl>(MAX_RESONANCE));
      return 1.0 / (SQRT2 * (1.0 - r));
    }

    lores::coefficients lores::Coefficients(Flt hz, Flt value, Flt sampleRate) {
      coefficients out;
      if (!(sampleRate > 0.f) || !std::isfinite(hz)) return out;
      const Dbl sr = static_cast<Dbl>(sampleRate);
      const Dbl f = std::clamp(static_cast<Dbl>(hz), 0.0, sr * 0.5);
      // A cutoff of 0 passes nothing; all zero also leaves no state behind.
      if (!(f > 0.0)) return out;
      const Dbl w = 2.0 * PI * f / sr;
      const Dbl q = Q(value);
      const Dbl r = std::exp(-w / (2.0 * q));
      const Dbl theta = w * std::sqrt(1.0 - 1.0 / (4.0 * q * q));
      out.a1 = -2.0 * r * std::cos(theta);
      out.a2 = r * r;
      out.g = 1.0 + out.a1 + out.a2;
      return out;
    }

    void lores::Update() {
      if (cachedRate != SAMPLERATE || cachedFreq != freq || cachedRes != res) {
        c = Coefficients(freq, res, static_cast<Flt>(SAMPLERATE));
        cachedRate = SAMPLERATE;
        cachedFreq = freq;
        cachedRes = res;
      }
    }

    lores::coefficients lores::coefficient() {
      Update();
      return c;
    }

    Flt lores::Step(Flt in) {
      if (std::isfinite(in)) {
        const Dbl y = c.g * static_cast<Dbl>(in) - c.a1 * y1 - c.a2 * y2;
        y2 = y1;
        y1 = y;
      }
      return static_cast<Flt>(y1);
    }

    Flt lores::process(Flt in) {
      Update();
      return Step(in);
    }

    buffer& lores::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      Update();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& lores::operator()(buffer& in, buffer& cutoffSignal, buffer& resonanceSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), cutoffSignal.getLength(), resonanceSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* f = cutoffSignal.getPtr();
      const Flt* q = resonanceSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        cutoff(f[i]);
        resonance(q[i]);
        Update();
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
