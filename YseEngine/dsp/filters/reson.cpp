#include "reson.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;

  constexpr Dbl PI = 3.14159265358979323846;

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

    reson::reson() : output(STANDARD_BUFFERSIZE) {}

    reson& reson::gain(Flt value) {
      if (std::isfinite(value)) amp = value;
      return *this;
    }

    reson& reson::frequency(Flt hz) {
      if (std::isfinite(hz)) freq = hz;
      return *this;
    }

    reson& reson::q(Flt value) {
      if (std::isfinite(value)) quality = value;
      return *this;
    }

    reson& reson::clear() {
      x1 = 0.0;
      x2 = 0.0;
      y1 = 0.0;
      y2 = 0.0;
      return *this;
    }

    reson::coefficients reson::Coefficients(Flt gain, Flt hz, Flt q, Flt sampleRate) {
      coefficients out;
      if (!(sampleRate > 0.f) || !std::isfinite(hz)) return out;
      const Dbl sr = static_cast<Dbl>(sampleRate);
      const Dbl f = std::clamp(static_cast<Dbl>(hz), 0.0, sr * 0.5);
      // A center frequency of 0 passes nothing; all zero also leaves no state
      // behind.
      if (!(f > 0.0)) return out;
      const Dbl qq = std::isfinite(q) ? std::clamp(static_cast<Dbl>(q), static_cast<Dbl>(MIN_Q),
                                                   static_cast<Dbl>(MAX_Q))
                                      : static_cast<Dbl>(MIN_Q);
      const Dbl r = std::exp(-PI * (f / qq) / sr);
      // Too narrow to tell from 1: the poles would sit on the unit circle.
      if (!(r < 1.0)) return out;
      out.r = r;
      out.b1 = 2.0 * r * std::cos(2.0 * PI * f / sr);
      out.b2 = -r * r;
      out.a0 = (std::isfinite(gain) ? static_cast<Dbl>(gain) : 0.0) * (1.0 - r);
      return out;
    }

    void reson::Update() {
      if (cachedRate != SAMPLERATE || cachedFreq != freq || cachedQ != quality) {
        c = Coefficients(amp, freq, quality, static_cast<Flt>(SAMPLERATE));
        cachedRate = SAMPLERATE;
        cachedFreq = freq;
        cachedQ = quality;
        cachedAmp = amp;
      } else if (cachedAmp != amp) {
        c.a0 = static_cast<Dbl>(amp) * (1.0 - c.r);
        // r is 0 only when nothing passes.
        if (c.r == 0.0) c.a0 = 0.0;
        cachedAmp = amp;
      }
    }

    reson::coefficients reson::coefficient() {
      Update();
      return c;
    }

    Flt reson::Step(Flt in) {
      if (std::isfinite(in)) {
        const Dbl x = static_cast<Dbl>(in);
        const Dbl y = c.a0 * (x - c.r * x2) + c.b1 * y1 + c.b2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
      }
      return static_cast<Flt>(y1);
    }

    Flt reson::process(Flt in) {
      Update();
      return Step(in);
    }

    buffer& reson::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      Update();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& reson::operator()(buffer& in, buffer& gainSignal, buffer& frequencySignal,
                              buffer& qSignal) {
      const UInt length = std::min({Clamp(in.getLength()), gainSignal.getLength(),
                                    frequencySignal.getLength(), qSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* g = gainSignal.getPtr();
      const Flt* f = frequencySignal.getPtr();
      const Flt* s = qSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        gain(g[i]);
        frequency(f[i]);
        q(s[i]);
        Update();
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
