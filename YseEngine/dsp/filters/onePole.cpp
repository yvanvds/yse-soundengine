#include "onePole.hpp"

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

    onePole::onePole() : output(STANDARD_BUFFERSIZE) {}

    onePole& onePole::frequency(Flt value) {
      if (std::isfinite(value)) freq = value;
      return *this;
    }

    onePole& onePole::mode(FREQUENCY_MODE m) {
      if (m == HZ || m == LINEAR || m == RADIANS) freqMode = m;
      return *this;
    }

    onePole& onePole::clear() {
      last = 0.f;
      return *this;
    }

    Flt onePole::Coefficient(Flt value, FREQUENCY_MODE mode, Flt sampleRate) {
      if (!std::isfinite(value)) value = 0.f;
      switch (mode) {
      case LINEAR:
        return static_cast<Flt>(std::sin(static_cast<Dbl>(std::clamp(value, 0.f, 1.f)) * PI * 0.5));
      case RADIANS:
        return std::clamp(value, 0.f, 1.f);
      case HZ:
      default:
        break;
      }
      if (!(sampleRate > 0.f)) return 0.f;
      const Dbl f = std::clamp(static_cast<Dbl>(value), 0.0, static_cast<Dbl>(sampleRate) * 0.25);
      // At exactly a quarter of the sample rate the sine is 1 up to rounding.
      return static_cast<Flt>(std::min(1.0, std::sin(2.0 * PI * f / static_cast<Dbl>(sampleRate))));
    }

    Flt onePole::coefficient() {
      if (cachedRate != SAMPLERATE || cachedFreq != freq || cachedMode != freqMode) {
        a0 = Coefficient(freq, freqMode, static_cast<Flt>(SAMPLERATE));
        cachedRate = SAMPLERATE;
        cachedFreq = freq;
        cachedMode = freqMode;
      }
      return a0;
    }

    Flt onePole::Step(Flt in, Flt a) {
      if (std::isfinite(in)) last += a * (in - last);
      return last;
    }

    Flt onePole::process(Flt in) {
      return Step(in, coefficient());
    }

    buffer& onePole::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt a = coefficient();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i], a);
      return output;
    }

    buffer& onePole::operator()(buffer& in, buffer& freqSignal) {
      if (freqSignal.getLength() > 0) frequency(freqSignal.getPtr()[0]);
      return (*this)(in);
    }

  } // namespace DSP
} // namespace YSE
