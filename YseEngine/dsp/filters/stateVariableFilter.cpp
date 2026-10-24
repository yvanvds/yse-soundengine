#include "stateVariableFilter.hpp"

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

    stateVariableFilter::stateVariableFilter()
      : low(STANDARD_BUFFERSIZE),
        high(STANDARD_BUFFERSIZE),
        band(STANDARD_BUFFERSIZE),
        reject(STANDARD_BUFFERSIZE) {}

    stateVariableFilter& stateVariableFilter::frequency(Flt value) {
      if (std::isfinite(value)) freq = value;
      return *this;
    }

    stateVariableFilter& stateVariableFilter::resonance(Flt value) {
      if (std::isfinite(value)) res = value;
      return *this;
    }

    stateVariableFilter& stateVariableFilter::mode(FREQUENCY_MODE m) {
      if (m == HZ || m == LINEAR || m == RADIANS) freqMode = m;
      return *this;
    }

    stateVariableFilter& stateVariableFilter::clear() {
      s1 = 0.0;
      s2 = 0.0;
      last = outputs();
      return *this;
    }

    Dbl stateVariableFilter::CenterFrequency(Flt value, FREQUENCY_MODE mode, Flt sampleRate) {
      if (!(sampleRate > 0.f) || !std::isfinite(value)) return 0.0;
      const Dbl sr = static_cast<Dbl>(sampleRate);
      const Dbl top = sr * 0.25;
      switch (mode) {
      case LINEAR:
        return std::clamp(static_cast<Dbl>(value), 0.0, 1.0) * top;
      case RADIANS:
        return std::min(top,
                        std::asin(std::clamp(static_cast<Dbl>(value), 0.0, 1.0)) * sr / (2.0 * PI));
      case HZ:
      default:
        return std::clamp(static_cast<Dbl>(value), 0.0, top);
      }
    }

    stateVariableFilter::coefficients stateVariableFilter::Coefficients(Flt value,
                                                                        FREQUENCY_MODE mode,
                                                                        Flt resonance,
                                                                        Flt sampleRate) {
      coefficients out;
      const Dbl r =
          std::isfinite(resonance) ? std::clamp(static_cast<Dbl>(resonance), 0.0, 1.0) : 0.0;
      out.k = std::max(2.0 * (1.0 - r), MIN_DAMPING);
      if (sampleRate > 0.f) {
        const Dbl fc = CenterFrequency(value, mode, sampleRate);
        out.g = std::tan(PI * fc / static_cast<Dbl>(sampleRate));
      }
      out.a1 = 1.0 / (1.0 + out.g * (out.g + out.k));
      out.a2 = out.g * out.a1;
      out.a3 = out.g * out.a2;
      return out;
    }

    void stateVariableFilter::Update() {
      if (cachedRate != SAMPLERATE || cachedFreq != freq || cachedRes != res ||
          cachedMode != freqMode) {
        c = Coefficients(freq, freqMode, res, static_cast<Flt>(SAMPLERATE));
        cachedRate = SAMPLERATE;
        cachedFreq = freq;
        cachedRes = res;
        cachedMode = freqMode;
        // At g = 0 the integrators would hold their state forever, leaving
        // a constant on the lowpass and bandpass: let it go instead.
        if (c.g == 0.0) {
          s1 = 0.0;
          s2 = 0.0;
        }
      }
    }

    stateVariableFilter::coefficients stateVariableFilter::coefficient() {
      Update();
      return c;
    }

    void stateVariableFilter::Step(Flt in) {
      if (!std::isfinite(in)) return;
      const Dbl x = static_cast<Dbl>(in);
      const Dbl v3 = x - s2;
      const Dbl v1 = c.a1 * s1 + c.a2 * v3;
      const Dbl v2 = s2 + c.a2 * s1 + c.a3 * v3;
      s1 = 2.0 * v1 - s1;
      s2 = 2.0 * v2 - s2;
      last.lowpass = static_cast<Flt>(v2);
      last.bandpass = static_cast<Flt>(v1);
      last.highpass = static_cast<Flt>(x - c.k * v1 - v2);
      last.notch = static_cast<Flt>(x - c.k * v1);
    }

    stateVariableFilter::outputs stateVariableFilter::process(Flt in) {
      Update();
      Step(in);
      return last;
    }

    // Within the capacity set in the constructor: no allocation.
    void stateVariableFilter::Fit(UInt length) {
      if (low.getLength() != length) low.resize(length);
      if (high.getLength() != length) high.resize(length);
      if (band.getLength() != length) band.resize(length);
      if (reject.getLength() != length) reject.resize(length);
    }

    buffer& stateVariableFilter::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(length);
      Update();
      const Flt* x = in.getPtr();
      Flt* lp = low.getPtr();
      Flt* hp = high.getPtr();
      Flt* bp = band.getPtr();
      Flt* nt = reject.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Step(x[i]);
        lp[i] = last.lowpass;
        hp[i] = last.highpass;
        bp[i] = last.bandpass;
        nt[i] = last.notch;
      }
      return low;
    }

    buffer& stateVariableFilter::operator()(buffer& in, buffer& frequencySignal,
                                            buffer& resonanceSignal) {
      const UInt length = std::min(
          {Clamp(in.getLength()), frequencySignal.getLength(), resonanceSignal.getLength()});
      Fit(length);
      const Flt* x = in.getPtr();
      const Flt* f = frequencySignal.getPtr();
      const Flt* r = resonanceSignal.getPtr();
      Flt* lp = low.getPtr();
      Flt* hp = high.getPtr();
      Flt* bp = band.getPtr();
      Flt* nt = reject.getPtr();
      for (UInt i = 0; i < length; ++i) {
        frequency(f[i]);
        resonance(r[i]);
        Update();
        Step(x[i]);
        lp[i] = last.lowpass;
        hp[i] = last.highpass;
        bp[i] = last.bandpass;
        nt[i] = last.notch;
      }
      return low;
    }

  } // namespace DSP
} // namespace YSE
