#include "bandLimitedRandom.hpp"

#include <cmath>
#include "whiteSource.hpp"

namespace {

  static_assert(YSE::DSP::bandLimitedRandom::DEFAULT_SEED == YSE::DSP::detail::WHITE_DEFAULT_SEED,
                "bandLimitedRandom starts from DSP::noise's seed");

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // The phase step for a frequency in Hz: the sign is ignored, at most one
  // segment per sample, and anything that is not a finite number holds.
  inline Dbl Increment(Dbl frequency, Dbl rate) {
    const Dbl step = std::fabs(frequency) / rate;
    if (!(step >= 0.0)) return 0.0; // NaN
    return step < 1.0 ? step : 1.0; // also catches infinity
  }

  struct constantStep {
    Dbl step;
    Dbl operator[](UInt) const {
      return step;
    }
  };

  struct signalStep {
    const Flt* values;
    Dbl rate;
    Dbl operator[](UInt i) const {
      return Increment(static_cast<Dbl>(values[i]), rate);
    }
  };

} // namespace

namespace YSE {
  namespace DSP {

    bandLimitedRandom::bandLimitedRandom()
      : state(DEFAULT_SEED), from(0.0), to(0.0), phase(0.0), output(STANDARD_BUFFERSIZE) {
      seed(DEFAULT_SEED);
    }

    bandLimitedRandom& bandLimitedRandom::seed(std::uint32_t value) {
      state = value;
      from = 0.0;
      to = detail::NextWhite(state);
      phase = 0.0;
      return *this;
    }

    template <class Frequency>
    buffer& bandLimitedRandom::process(Frequency frequency, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      Flt* out = output.getPtr();

      for (UInt i = 0; i < length; ++i) {
        out[i] = static_cast<Flt>(from + (to - from) * phase);
        // The step is at most 1 and phase below 1, so one wrap is enough.
        phase += frequency[i];
        if (phase >= 1.0) {
          phase -= 1.0;
          from = to;
          to = detail::NextWhite(state);
        }
      }
      return output;
    }

    buffer& bandLimitedRandom::operator()(Flt frequency, UInt length) {
      const Dbl rate = static_cast<Dbl>(SAMPLERATE);
      return process(constantStep{Increment(static_cast<Dbl>(frequency), rate)}, length);
    }

    buffer& bandLimitedRandom::operator()(buffer& frequency) {
      return process(signalStep{frequency.getPtr(), static_cast<Dbl>(SAMPLERATE)},
                     frequency.getLength());
    }

  } // namespace DSP
} // namespace YSE
