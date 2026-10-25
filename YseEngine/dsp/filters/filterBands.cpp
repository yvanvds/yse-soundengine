#include "filterBands.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {
  constexpr Dbl PI = 3.14159265358979323846;

  inline bool Finite(const YSE::DSP::biQuad::coefficientSet& c) {
    return std::isfinite(c.a0) && std::isfinite(c.a1) && std::isfinite(c.a2) &&
           std::isfinite(c.b1) && std::isfinite(c.b2);
  }

  inline Flt Rate(Flt sampleRate) {
    return sampleRate > 0.f ? sampleRate : static_cast<Flt>(YSE::SAMPLERATE);
  }
} // namespace

namespace YSE {
  namespace DSP {

    filterBands::filterBands() = default;

    Flt filterBands::Bound(const band& b, UInt which, Flt value) {
      const Flt low = b.limits[2 * which];
      const Flt high = b.limits[2 * which + 1];
      if (low != 0.f && value < low) value = low;
      if (high != 0.f && value > high) value = high;
      return value;
    }

    filterBands& filterBands::bands(UInt value) {
      count = std::clamp<UInt>(value, 1, MAX_BANDS);
      return *this;
    }

    filterBands& filterBands::mode(UInt index, MODE value) {
      const int m = static_cast<int>(value);
      if (index >= MAX_BANDS || m < DISPLAY || m >= MODE_COUNT) return *this;
      band& b = slots[index];
      // Into DISPLAY: the band keeps sounding as it did.
      if (value == DISPLAY && b.mode != DISPLAY) b.raw = coefficients(index);
      b.mode = value;
      return *this;
    }

    filterBands& filterBands::parameters(UInt index, Flt hz, Flt value, Flt quality) {
      frequency(index, hz);
      gain(index, value);
      q(index, quality);
      return *this;
    }

    filterBands& filterBands::frequency(UInt index, Flt hz) {
      if (index < MAX_BANDS && std::isfinite(hz))
        slots[index].frequency = Bound(slots[index], 0, hz);
      return *this;
    }

    filterBands& filterBands::gain(UInt index, Flt value) {
      if (index < MAX_BANDS && std::isfinite(value))
        slots[index].gain = Bound(slots[index], 1, value);
      return *this;
    }

    filterBands& filterBands::q(UInt index, Flt value) {
      if (index < MAX_BANDS && std::isfinite(value)) slots[index].q = Bound(slots[index], 2, value);
      return *this;
    }

    filterBands& filterBands::gainEnabled(UInt index, Bool on) {
      if (index < MAX_BANDS) slots[index].gainEnabled = on;
      return *this;
    }

    filterBands& filterBands::handle(UInt index, Bool on) {
      if (index < MAX_BANDS) slots[index].handle = on;
      return *this;
    }

    filterBands& filterBands::constraints(UInt index, Flt minFrequency, Flt maxFrequency,
                                          Flt minGain, Flt maxGain, Flt minQ, Flt maxQ) {
      if (index >= MAX_BANDS) return *this;
      const std::array<Flt, 6> values{minFrequency, maxFrequency, minGain, maxGain, minQ, maxQ};
      for (const Flt v : values)
        if (!std::isfinite(v)) return *this;
      band& b = slots[index];
      b.limits = values;
      for (UInt i = 0; i < 3; ++i) {
        Flt& low = b.limits[2 * i];
        Flt& high = b.limits[2 * i + 1];
        if (low != 0.f && high != 0.f && low > high) std::swap(low, high);
      }
      b.frequency = Bound(b, 0, b.frequency);
      b.gain = Bound(b, 1, b.gain);
      b.q = Bound(b, 2, b.q);
      return *this;
    }

    filterBands& filterBands::coefficients(UInt index, const coefficientSet& set) {
      if (index < MAX_BANDS && Finite(set)) slots[index].raw = set;
      return *this;
    }

    filterBands& filterBands::coefficient(UInt index, UInt which, Flt value) {
      if (index >= MAX_BANDS || which > 4 || !std::isfinite(value)) return *this;
      coefficientSet& c = slots[index].raw;
      Flt* fields[5] = {&c.a0, &c.a1, &c.a2, &c.b1, &c.b2};
      *fields[which] = value;
      return *this;
    }

    UInt filterBands::cascade(const Flt* values, UInt valueCount) {
      if (values == nullptr) return 0;
      const UInt sets = std::min<UInt>(valueCount / 5, MAX_BANDS);
      if (sets == 0) return 0;
      for (UInt i = 0; i < sets * 5; ++i)
        if (!std::isfinite(values[i])) return 0;
      for (UInt s = 0; s < sets; ++s) {
        const Flt* v = values + 5 * s;
        slots[s].raw = coefficientSet{v[0], v[1], v[2], v[3], v[4]};
        slots[s].mode = DISPLAY;
      }
      count = sets;
      return sets;
    }

    filterBands& filterBands::flat(UInt index) {
      if (index >= MAX_BANDS) return *this;
      band& b = slots[index];
      switch (b.mode) {
      case LOWPASS:
      case HIGHPASS:
        b.q = Bound(b, 2, FLAT_Q);
        b.gain = Bound(b, 1, 1.f);
        break;
      case BANDPASS:
      case BANDSTOP:
        break;
      default:
        b.gain = Bound(b, 1, 1.f);
        break;
      }
      return *this;
    }

    filterBands::MODE filterBands::mode(UInt index) const {
      return index < MAX_BANDS ? slots[index].mode : LOWPASS;
    }

    Flt filterBands::frequency(UInt index) const {
      return index < MAX_BANDS ? slots[index].frequency : band().frequency;
    }

    Flt filterBands::gain(UInt index) const {
      return index < MAX_BANDS ? slots[index].gain : band().gain;
    }

    Flt filterBands::q(UInt index) const {
      return index < MAX_BANDS ? slots[index].q : band().q;
    }

    Bool filterBands::gainEnabled(UInt index) const {
      return index < MAX_BANDS && slots[index].gainEnabled;
    }

    Bool filterBands::handle(UInt index) const {
      return index < MAX_BANDS && slots[index].handle;
    }

    Flt filterBands::constraint(UInt index, UInt which) const {
      return index < MAX_BANDS && which < 6 ? slots[index].limits[which] : 0.f;
    }

    Flt filterBands::bandwidth(UInt index) const {
      const Flt quality =
          std::clamp(q(index), filterCoefficients::MIN_Q, filterCoefficients::MAX_Q);
      return frequency(index) / quality;
    }

    filterBands::coefficientSet filterBands::coefficients(UInt index, Flt sampleRate) const {
      if (index >= MAX_BANDS) return band().raw;
      const band& b = slots[index];
      if (b.mode == DISPLAY) return b.raw;
      int type = static_cast<int>(b.mode);
      if (b.gainEnabled) {
        switch (b.mode) {
        case LOWPASS:
          type = filterCoefficients::GAINLPASS;
          break;
        case HIGHPASS:
          type = filterCoefficients::GAINHPASS;
          break;
        case BANDPASS:
          type = filterCoefficients::GAINBPASS;
          break;
        case BANDSTOP:
          type = filterCoefficients::GAINBSTOP;
          break;
        case RESONANT:
          type = filterCoefficients::GAINRESONANT;
          break;
        default:
          break;
        }
      }
      if (b.mode == ALLPASS) type = filterCoefficients::GAINAPASS;
      return filterCoefficients::Compute(static_cast<filterCoefficients::TYPE>(type), b.frequency,
                                         b.gain, b.q, Rate(sampleRate));
    }

    UInt filterBands::sections(Flt* out, UInt capacity, Flt sampleRate) const {
      if (out == nullptr) return 0;
      const UInt n = std::min<UInt>(count, capacity / 5);
      for (UInt s = 0; s < n; ++s) {
        const coefficientSet c = coefficients(s, sampleRate);
        out[5 * s] = c.a0;
        out[5 * s + 1] = c.a1;
        out[5 * s + 2] = c.a2;
        out[5 * s + 3] = c.b1;
        out[5 * s + 4] = c.b2;
      }
      return n * 5;
    }

    Bool filterBands::responseAt(Flt hz, Flt& amplitude, Flt& phase, Flt sampleRate) const {
      const Flt rate = Rate(sampleRate);
      if (!std::isfinite(hz) || !(rate > 0.f) || !std::isfinite(rate)) return false;
      const Dbl w = 2.0 * PI * static_cast<Dbl>(hz) / static_cast<Dbl>(rate);
      const std::complex<Dbl> z1 = std::polar(1.0, -w);
      const std::complex<Dbl> z2 = z1 * z1;
      std::complex<Dbl> h(1.0, 0.0);
      for (UInt s = 0; s < count; ++s) {
        const coefficientSet c = coefficients(s, rate);
        const std::complex<Dbl> num =
            static_cast<Dbl>(c.a0) + static_cast<Dbl>(c.a1) * z1 + static_cast<Dbl>(c.a2) * z2;
        const std::complex<Dbl> den =
            1.0 + static_cast<Dbl>(c.b1) * z1 + static_cast<Dbl>(c.b2) * z2;
        h *= num / den;
      }
      amplitude = static_cast<Flt>(std::abs(h));
      phase = static_cast<Flt>(std::arg(h));
      return true;
    }

    Bool filterBands::response(filterResponse& curves, Flt sampleRate) const {
      Flt values[MAX_BANDS * 5];
      const UInt n = sections(values, MAX_BANDS * 5, sampleRate);
      return curves.analyse(values, n);
    }

  } // namespace DSP
} // namespace YSE
