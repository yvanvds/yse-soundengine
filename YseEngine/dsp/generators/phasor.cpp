#include "phasor.hpp"

#include <cmath>

namespace {
  // Wrap into [0, 1). floor keeps negative phases (a ramp running backwards)
  // on the same cycle grid as positive ones. The guard catches the one input
  // where rounding lands exactly on 1: a value a hair below 0.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  // The double phase is below 1, but the nearest float to 0.99999999 is 1.0f.
  // Keep the output below 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasor::phasor() : current(0.0), output(STANDARD_BUFFERSIZE) {}

    phasor& phasor::phase(Flt value) {
      current = Wrap(static_cast<Dbl>(value));
      return *this;
    }

    Flt phasor::phase() const {
      return ToSample(current);
    }

    buffer& phasor::operator()(Flt frequency, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);

      const Dbl increment = static_cast<Dbl>(frequency) / static_cast<Dbl>(SAMPLERATE);
      Flt* out = output.getPtr();
      Dbl p = current;
      for (UInt i = 0; i < length; ++i) {
        out[i] = ToSample(p);
        p = Wrap(p + increment);
      }
      current = p;
      return output;
    }

    buffer& phasor::operator()(buffer& frequency) {
      const UInt length = Clamp(frequency.getLength());
      if (output.getLength() != length) output.resize(length);

      const Dbl toIncrement = 1.0 / static_cast<Dbl>(SAMPLERATE);
      const Flt* in = frequency.getPtr();
      Flt* out = output.getPtr();
      Dbl p = current;
      for (UInt i = 0; i < length; ++i) {
        out[i] = ToSample(p);
        p = Wrap(p + static_cast<Dbl>(in[i]) * toIncrement);
      }
      current = p;
      return output;
    }

  } // namespace DSP
} // namespace YSE
