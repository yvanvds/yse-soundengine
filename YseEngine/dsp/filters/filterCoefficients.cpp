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
      const preciseSet precise = ComputePrecise(type, frequency, gain, q, sampleRate);
      coefficientSet out;
      out.a0 = static_cast<Flt>(precise.a0);
      out.a1 = static_cast<Flt>(precise.a1);
      out.a2 = static_cast<Flt>(precise.a2);
      out.b1 = static_cast<Flt>(precise.b1);
      out.b2 = static_cast<Flt>(precise.b2);
      return out;
    }

    filterCoefficients::preciseSet
    filterCoefficients::ComputePrecise(TYPE type, Flt frequency, Flt gain, Flt q, Flt sampleRate) {
      preciseSet out;
      // As an int: a TYPE cast from any number reaches here.
      const int index = static_cast<int>(type);
      if (index <= OFF || index >= TYPE_COUNT) {
        out.a0 = 1.0;
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
      out.a0 = g * b0 * norm;
      out.a1 = g * b1 * norm;
      out.a2 = g * b2 * norm;
      out.b1 = a1 * norm;
      out.b2 = a2 * norm;
      return out;
    }

    filterCoefficients::coefficientSet
    filterCoefficients::ComputeMatched(TYPE type, Flt frequency, Flt gain, Flt q, Flt sampleRate) {
      const preciseSet precise = ComputeMatchedPrecise(type, frequency, gain, q, sampleRate);
      coefficientSet out;
      out.a0 = static_cast<Flt>(precise.a0);
      out.a1 = static_cast<Flt>(precise.a1);
      out.a2 = static_cast<Flt>(precise.a2);
      out.b1 = static_cast<Flt>(precise.b1);
      out.b2 = static_cast<Flt>(precise.b2);
      return out;
    }

    filterCoefficients::preciseSet filterCoefficients::ComputeMatchedPrecise(TYPE type,
                                                                             Flt frequency,
                                                                             Flt gain, Flt q,
                                                                             Flt sampleRate) {
      const int index = static_cast<int>(type);
      const bool peak = index == PEAKNOTCH;
      if (!peak && index != BANDPASS && index != GAINBPASS)
        return ComputePrecise(type, frequency, gain, q, sampleRate);
      preciseSet out;
      if (!(sampleRate > 0.f) || !std::isfinite(sampleRate) || !std::isfinite(frequency) ||
          !std::isfinite(gain) || !std::isfinite(q))
        return out;

      const Dbl w0 = std::clamp(
          2.0 * PI * static_cast<Dbl>(frequency) / static_cast<Dbl>(sampleRate), EDGE, PI - EDGE);
      const Dbl qq =
          std::clamp(static_cast<Dbl>(q), static_cast<Dbl>(MIN_Q), static_cast<Dbl>(MAX_Q));
      const Dbl asked = std::sqrt(std::max(static_cast<Dbl>(gain), static_cast<Dbl>(MIN_GAIN)));
      // A cut is the inverse of the boost by its reciprocal, prototype and
      // biquad alike: designed as that boost and turned over, since a deep
      // cut near half the sample rate cannot match its three points
      // directly. The boost's zeroes are inside the unit circle (both its
      // sums and b0 >= |b2| are positive), so the cut's poles are too.
      const bool invert = peak && asked < 1.0;
      const Dbl A = invert ? 1.0 / asked : asked;
      // The Q of the poles and, for the peak, of the zeroes.
      const Dbl qPoles = peak ? qq * A : qq;
      const Dbl qZeroes = qq / A;

      // The prototype's |H|^2 at w.
      const auto prototype = [&](Dbl w) {
        const Dbl v = w / w0;
        const Dbl d = (1.0 - v * v) * (1.0 - v * v);
        const Dbl p = (v / qPoles) * (v / qPoles);
        if (!peak) return p / (d + p);
        const Dbl z = (v / qZeroes) * (v / qZeroes);
        return (d + z) / (d + p);
      };

      // Poles: s = w0 (-zeta +- sqrt(zeta^2 - 1)) through z = exp(s).
      const Dbl zeta = 1.0 / (2.0 * qPoles);
      Dbl p1 = 0.0;
      if (zeta <= 1.0) {
        p1 = -2.0 * std::exp(-zeta * w0) * std::cos(std::sqrt(1.0 - zeta * zeta) * w0);
      } else {
        // Two real poles, each exponentiated on its own: exp times cosh
        // overflows for a low Q.
        const Dbl root = std::sqrt(zeta * zeta - 1.0);
        p1 = -(std::exp(-w0 / (zeta + root)) + std::exp(-w0 * (zeta + root)));
      }
      const Dbl p2 = std::exp(-2.0 * zeta * w0);

      // |b0 + b1 z^-1 + b2 z^-2|^2 = (b0 + b1 + b2)^2 phi0 + (b0 - b1 + b2)^2
      // phi1 - 4 b0 b2 phi2, with phi1 = sin^2(w / 2), phi0 = 1 - phi1 and
      // phi2 = 4 phi0 phi1; the same for the poles with b0 = 1.
      const Dbl A0 = (1.0 + p1 + p2) * (1.0 + p1 + p2);
      const Dbl A1 = (1.0 - p1 + p2) * (1.0 - p1 + p2);
      const Dbl A2 = -4.0 * p2;
      const Dbl half = std::sin(w0 / 2.0);
      const Dbl phi1 = half * half;
      const Dbl phi0 = 1.0 - phi1;
      const Dbl phi2 = 4.0 * phi0 * phi1;

      // The numerator's three terms from the prototype at 0, pi and w0.
      const Dbl B0 = peak ? A0 : 0.0;
      const Dbl B1 = A1 * prototype(PI);
      const Dbl atW0 = peak ? A * A * A * A : 1.0;
      const Dbl B2 = (atW0 * (A0 * phi0 + A1 * phi1 + A2 * phi2) - B0 * phi0 - B1 * phi1) / phi2;

      const Dbl r0 = std::sqrt(B0);
      const Dbl r1 = std::sqrt(B1);
      const Dbl W = 0.5 * (r0 + r1);
      const Dbl b0 = 0.5 * (W + std::sqrt(std::max(W * W + B2, 0.0)));
      const Dbl b1 = 0.5 * (r0 - r1);
      const Dbl b2 = b0 > 0.0 ? -B2 / (4.0 * b0) : 0.0;
      if (invert) {
        const Dbl norm = 1.0 / b0;
        out.a0 = norm;
        out.a1 = p1 * norm;
        out.a2 = p2 * norm;
        out.b1 = b1 * norm;
        out.b2 = b2 * norm;
        return out;
      }
      const Dbl g = index == GAINBPASS ? static_cast<Dbl>(gain) : 1.0;
      out.a0 = g * b0;
      out.a1 = g * b1;
      out.a2 = g * b2;
      out.b1 = p1;
      out.b2 = p2;
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
