#include "filterBank.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // A frequency spread can overflow a float: hold it at the largest one,
  // which the coefficients clamp below the Nyquist frequency.
  inline Flt Finite(Dbl hz) {
    constexpr auto top = static_cast<Dbl>(std::numeric_limits<Flt>::max());
    return static_cast<Flt>(std::clamp(hz, -top, top));
  }
} // namespace

namespace YSE {
  namespace DSP {

    filterBank::filterBank(UInt filters) {
      const UInt count = std::clamp<UInt>(filters, 1, MAX_FILTERS);
      bank.resize(count);
      outputs.reserve(count);
      for (UInt i = 0; i < count; ++i)
        outputs.emplace_back(STANDARD_BUFFERSIZE);
      frequencyRatio(DEFAULT_FREQUENCY, DEFAULT_RATIO);
    }

    UInt filterBank::Index(UInt index) const {
      return index < size() ? index : size() - 1;
    }

    filterBank& filterBank::frequency(UInt index, Flt hz) {
      if (index < size() && std::isfinite(hz) && bank[index].freq != hz) {
        bank[index].freq = hz;
        bank[index].dirty = true;
      }
      return *this;
    }

    filterBank& filterBank::q(UInt index, Flt value) {
      if (index < size() && std::isfinite(value) && bank[index].quality != value) {
        bank[index].quality = value;
        bank[index].dirty = true;
      }
      return *this;
    }

    filterBank& filterBank::gain(UInt index, Flt value) {
      if (index < size() && std::isfinite(value) && bank[index].amp != value) {
        bank[index].amp = value;
        bank[index].dirty = true;
      }
      return *this;
    }

    filterBank& filterBank::frequencyAll(Flt hz) {
      for (UInt i = 0; i < size(); ++i)
        frequency(i, hz);
      return *this;
    }

    filterBank& filterBank::qAll(Flt value) {
      for (UInt i = 0; i < size(); ++i)
        q(i, value);
      return *this;
    }

    filterBank& filterBank::gainAll(Flt value) {
      for (UInt i = 0; i < size(); ++i)
        gain(i, value);
      return *this;
    }

    Flt filterBank::RatioFrequency(Flt first, Flt ratio, UInt index) {
      return Finite(static_cast<Dbl>(first) *
                    std::pow(static_cast<Dbl>(ratio), static_cast<Dbl>(index)));
    }

    Flt filterBank::HarmonicFrequency(Flt first, UInt index) {
      return Finite(static_cast<Dbl>(first) * (static_cast<Dbl>(index) + 1.0));
    }

    filterBank& filterBank::frequencyRatio(Flt first, Flt ratio) {
      if (!std::isfinite(first) || !std::isfinite(ratio) || ratio <= 0.f) return *this;
      for (UInt i = 0; i < size(); ++i)
        frequency(i, RatioFrequency(first, ratio, i));
      return *this;
    }

    filterBank& filterBank::harmonic(Flt first) {
      if (!std::isfinite(first)) return *this;
      for (UInt i = 0; i < size(); ++i)
        frequency(i, HarmonicFrequency(first, i));
      return *this;
    }

    filterBank& filterBank::clear() {
      for (filter& f : bank) {
        f.s1 = 0.0;
        f.s2 = 0.0;
        f.last = 0.f;
      }
      return *this;
    }

    Flt filterBank::frequency(UInt index) const {
      return bank[Index(index)].freq;
    }

    Flt filterBank::q(UInt index) const {
      return bank[Index(index)].quality;
    }

    Flt filterBank::gain(UInt index) const {
      return bank[Index(index)].amp;
    }

    filterBank::coefficientSet filterBank::Coefficients(Flt hz, Flt q, Flt gain, Flt sampleRate) {
      return filterCoefficients::Compute(filterCoefficients::GAINBPASS, hz, gain, q, sampleRate);
    }

    filterBank::coefficientSet filterBank::coefficients(UInt index) const {
      const filter& f = bank[Index(index)];
      return Coefficients(f.freq, f.quality, f.amp, static_cast<Flt>(SAMPLERATE));
    }

    void filterBank::Update() {
      const bool rateChanged = cachedRate != SAMPLERATE;
      cachedRate = SAMPLERATE;
      for (filter& f : bank) {
        if (!f.dirty && !rateChanged) continue;
        const coefficientSet c =
            Coefficients(f.freq, f.quality, f.amp, static_cast<Flt>(SAMPLERATE));
        f.a0 = c.a0;
        f.a1 = c.a1;
        f.a2 = c.a2;
        f.b1 = c.b1;
        f.b2 = c.b2;
        f.dirty = false;
      }
    }

    // Within the capacity set in the constructor: no allocation.
    void filterBank::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Update();
      const Flt* x = in.getPtr();
      for (UInt n = 0; n < size(); ++n) {
        filter& f = bank[n];
        buffer& out = outputs[n];
        if (out.getLength() != length) out.resize(length);
        Flt* y = out.getPtr();
        for (UInt i = 0; i < length; ++i) {
          if (!std::isfinite(x[i])) {
            y[i] = f.last;
            continue;
          }
          const Dbl v = static_cast<Dbl>(x[i]);
          const Dbl r = f.a0 * v + f.s1;
          f.s1 = f.a1 * v - f.b1 * r + f.s2;
          f.s2 = f.a2 * v - f.b2 * r;
          auto result = static_cast<Flt>(r);
          if (!std::isfinite(result) || !std::isfinite(f.s1) || !std::isfinite(f.s2)) {
            f.s1 = 0.0;
            f.s2 = 0.0;
            result = 0.f;
          }
          f.last = result;
          y[i] = result;
        }
      }
    }

    buffer& filterBank::output(UInt index) {
      return outputs[Index(index)];
    }

  } // namespace DSP
} // namespace YSE
