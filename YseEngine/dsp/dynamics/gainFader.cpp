#include "gainFader.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

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

    gainFader::gainFader() : output(STANDARD_BUFFERSIZE) {}

    Flt gainFader::Gain(Int position, Int size, Flt scale, Flt increment) {
      if (position <= 0) return 0.f;
      return static_cast<Flt>(static_cast<Dbl>(scale) *
                              std::pow(static_cast<Dbl>(increment),
                                       static_cast<Dbl>(position) - static_cast<Dbl>(size)));
    }

    Int gainFader::Position(Flt position, Int size) {
      // A NaN fails the comparison and reads as 0 too.
      if (!(position >= 1.f)) return 0;
      const Int top = size - 1;
      if (position >= static_cast<Flt>(top)) return top;
      return static_cast<Int>(position);
    }

    Int gainFader::Size(Flt steps) {
      if (std::isnan(steps)) return DEFAULT_SIZE;
      if (steps < 2.f) return 2;
      if (steps >= static_cast<Flt>(MAX_SIZE)) return MAX_SIZE;
      return static_cast<Int>(steps);
    }

    gainFader& gainFader::position(Flt value) {
      if (!std::isnan(value)) slider = Position(value, steps);
      return *this;
    }

    gainFader& gainFader::size(Flt value) {
      if (std::isnan(value)) return *this;
      steps = Size(value);
      if (slider > steps - 1) slider = steps - 1;
      return *this;
    }

    gainFader& gainFader::scale(Flt value) {
      if (std::isfinite(value)) base = value;
      return *this;
    }

    gainFader& gainFader::increment(Flt value) {
      if (std::isfinite(value) && value > 0.f) factor = value;
      return *this;
    }

    gainFader& gainFader::time(Flt ms) {
      if (std::isnan(ms)) return *this;
      interpolation = ms > 0.f ? ms : 0.f;
      return *this;
    }

    buffer& gainFader::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      // The time follows the live sample rate, so it is worked out every
      // call; rampSmooth jumps for anything below one sample.
      smoother.ramp(static_cast<Flt>(static_cast<Dbl>(interpolation) * SAMPLERATE / 1000.0));
      const Flt target = gain();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = x[i] * smoother.process(target);
      return output;
    }

  } // namespace DSP
} // namespace YSE
