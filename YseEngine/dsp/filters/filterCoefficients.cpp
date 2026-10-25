#include "filterCoefficients.hpp"

#include <algorithm>
#include <cmath>

namespace {
  constexpr Dbl PI = 3.14159265358979323846;
  // How close to 0 and to half the sample rate the frequency may come, in
  // radians per sample: at either end sin(w) = 0 and the poles reach the
  // unit circle.
  constexpr Dbl EDGE = 1e-6;
  // The least the shelf slope's radicand may be: 0 would put the poles on
  // the unit circle, and much less than this too close to it for a float.
  constexpr Dbl MIN_RADICAND = 1e-4;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    filterCoefficients::coefficientSet
    filterCoefficients::Compute(TYPE type, Flt frequency, Flt gain, Flt q, Flt sampleRate) {
      coefficientSet out;
      // As an int: a TYPE cast from any number reaches here.
      const int index = static_cast<int>(type);
      if (index <= OFF || index >= TYPE_COUNT) {
        out.a0 = 1.f;
        return out;
      }
      if (!(sampleRate > 0.f) || !std::isfinite(sampleRate) || !std::isfinite(frequency) ||
          !std::isfinite(gain) || !std::isfinite(q))
        return out;

      const Dbl w = std::clamp(
          2.0 * PI * static_cast<Dbl>(frequency) / static_cast<Dbl>(sampleRate), EDGE, PI - EDGE);
      const Dbl qq =
          std::clamp(static_cast<Dbl>(q), static_cast<Dbl>(MIN_Q), static_cast<Dbl>(MAX_Q));
      const Dbl cw = std::cos(w);
      const Dbl sw = std::sin(w);
      const Dbl alpha = sw / (2.0 * qq);
      const Dbl A = std::sqrt(std::max(static_cast<Dbl>(gain), static_cast<Dbl>(MIN_GAIN)));

      // The cookbook's b0 b1 b2 (feedforward) over a0 a1 a2 (feedback).
      Dbl b0 = 1.0, b1 = 0.0, b2 = 0.0;
      Dbl a0 = 1.0, a1 = 0.0, a2 = 0.0;
      const auto poles = [&](Dbl alphaValue) {
        a0 = 1.0 + alphaValue;
        a1 = -2.0 * cw;
        a2 = 1.0 - alphaValue;
      };
      const auto shelfAlpha = [&]() {
        const Dbl radicand = (A + 1.0 / A) * (1.0 / qq - 1.0) + 2.0;
        return sw / 2.0 * std::sqrt(std::max(radicand, MIN_RADICAND));
      };

      bool scaled = false;
      switch (type) {
      case GAINLPASS:
        scaled = true;
        [[fallthrough]];
      case LOWPASS:
        b0 = (1.0 - cw) / 2.0;
        b1 = 1.0 - cw;
        b2 = b0;
        poles(alpha);
        break;
      case GAINHPASS:
        scaled = true;
        [[fallthrough]];
      case HIGHPASS:
        b0 = (1.0 + cw) / 2.0;
        b1 = -(1.0 + cw);
        b2 = b0;
        poles(alpha);
        break;
      case GAINBPASS:
        scaled = true;
        [[fallthrough]];
      case BANDPASS:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        poles(alpha);
        break;
      case GAINBSTOP:
        scaled = true;
        [[fallthrough]];
      case BANDSTOP:
        b0 = 1.0;
        b1 = -2.0 * cw;
        b2 = 1.0;
        poles(alpha);
        break;
      case GAINRESONANT:
        scaled = true;
        [[fallthrough]];
      case RESONANT:
        b0 = qq * alpha;
        b1 = 0.0;
        b2 = -qq * alpha;
        poles(alpha);
        break;
      case GAINAPASS:
        scaled = true;
        [[fallthrough]];
      case ALLPASS:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cw;
        b2 = 1.0 + alpha;
        poles(alpha);
        break;
      case PEAKNOTCH:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / A;
        break;
      case LOWSHELF: {
        const Dbl k = 2.0 * std::sqrt(A) * shelfAlpha();
        b0 = A * ((A + 1.0) - (A - 1.0) * cw + k);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
        b2 = A * ((A + 1.0) - (A - 1.0) * cw - k);
        a0 = (A + 1.0) + (A - 1.0) * cw + k;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
        a2 = (A + 1.0) + (A - 1.0) * cw - k;
        break;
      }
      case HIGHSHELF: {
        const Dbl k = 2.0 * std::sqrt(A) * shelfAlpha();
        b0 = A * ((A + 1.0) + (A - 1.0) * cw + k);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
        b2 = A * ((A + 1.0) + (A - 1.0) * cw - k);
        a0 = (A + 1.0) - (A - 1.0) * cw + k;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
        a2 = (A + 1.0) - (A - 1.0) * cw - k;
        break;
      }
      default:
        break;
      }

      const Dbl norm = 1.0 / a0;
      const Dbl g = scaled ? static_cast<Dbl>(gain) : 1.0;
      out.a0 = static_cast<Flt>(g * b0 * norm);
      out.a1 = static_cast<Flt>(g * b1 * norm);
      out.a2 = static_cast<Flt>(g * b2 * norm);
      out.b1 = static_cast<Flt>(a1 * norm);
      out.b2 = static_cast<Flt>(a2 * norm);
      return out;
    }

    // The outputs default to STANDARD_BUFFERSIZE samples.
    filterCoefficients::filterCoefficients() = default;

    filterCoefficients& filterCoefficients::type(TYPE value) {
      const int index = static_cast<int>(value);
      if (index >= OFF && index < TYPE_COUNT) filter = value;
      return *this;
    }

    filterCoefficients& filterCoefficients::frequency(Flt hz) {
      if (std::isfinite(hz)) freq = hz;
      return *this;
    }

    filterCoefficients& filterCoefficients::gain(Flt value) {
      if (std::isfinite(value)) amp = value;
      return *this;
    }

    filterCoefficients& filterCoefficients::q(Flt value) {
      if (std::isfinite(value)) quality = value;
      return *this;
    }

    filterCoefficients& filterCoefficients::resampling(UInt value) {
      factor = std::clamp<UInt>(value, 1, MAX_RESAMPLING);
      phase = 0;
      return *this;
    }

    filterCoefficients::coefficientSet filterCoefficients::coefficients() const {
      return Compute(filter, freq, amp, quality, static_cast<Flt>(SAMPLERATE));
    }

    buffer& filterCoefficients::output(UInt index) {
      return out[index < 5 ? index : 4];
    }

    void filterCoefficients::Update() {
      if (valid && cachedType == filter && cachedFreq == freq && cachedAmp == amp &&
          cachedQ == quality && cachedRate == SAMPLERATE)
        return;
      held = Compute(filter, freq, amp, quality, static_cast<Flt>(SAMPLERATE));
      valid = true;
      cachedType = filter;
      cachedFreq = freq;
      cachedAmp = amp;
      cachedQ = quality;
      cachedRate = SAMPLERATE;
    }

    void filterCoefficients::Emit(UInt i) {
      if (phase == 0) Update();
      if (++phase >= factor) phase = 0;
      out[0].getPtr()[i] = held.a0;
      out[1].getPtr()[i] = held.a1;
      out[2].getPtr()[i] = held.a2;
      out[3].getPtr()[i] = held.b1;
      out[4].getPtr()[i] = held.b2;
    }

    void filterCoefficients::Fit(UInt length) {
      // Within the capacity set in the constructor: no allocation.
      for (auto& b : out)
        if (b.getLength() != length) b.resize(length);
    }

    void filterCoefficients::operator()(UInt length) {
      length = Clamp(length);
      Fit(length);
      for (UInt i = 0; i < length; ++i)
        Emit(i);
    }

    void filterCoefficients::operator()(buffer& frequencySignal, buffer& gainSignal,
                                        buffer& qSignal) {
      const UInt length = std::min(
          {Clamp(frequencySignal.getLength()), gainSignal.getLength(), qSignal.getLength()});
      Fit(length);
      const Flt* f = frequencySignal.getPtr();
      const Flt* g = gainSignal.getPtr();
      const Flt* s = qSignal.getPtr();
      for (UInt i = 0; i < length; ++i) {
        frequency(f[i]);
        gain(g[i]);
        q(s[i]);
        Emit(i);
      }
    }

  } // namespace DSP
} // namespace YSE
