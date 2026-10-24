#include "polar.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Shorter(const buffer& a, const buffer& b) {
    return Clamp(a.getLength() < b.getLength() ? a.getLength() : b.getLength());
  }

  inline Flt Finite(Flt x) {
    return std::isfinite(x) ? x : 0.f;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline void ToPolar(Flt re, Flt im, Flt& magnitude, Flt& phase) {
    // hypot rather than sqrt(re*re + im*im): it does not overflow for operands
    // whose squares would.
    magnitude = Finite(std::hypot(re, im));
    phase = Finite(std::atan2(im, re));
  }

  inline void ToCart(Flt magnitude, Flt phase, Flt& re, Flt& im) {
    re = Finite(magnitude * std::cos(phase));
    im = Finite(magnitude * std::sin(phase));
  }

  inline Flt Wrap(Flt in) {
    if (!std::isfinite(in)) return 0.f;
    const double x = in;
    return static_cast<Flt>(x - kTwoPi * std::floor((x + kPi) / kTwoPi));
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── cartToPolar ─────────────────────────────────────────────────────────

    cartToPolar::cartToPolar() : magnitudeOut(STANDARD_BUFFERSIZE), phaseOut(STANDARD_BUFFERSIZE) {}

    void cartToPolar::process(Flt re, Flt im, Flt& magnitude, Flt& phase) {
      ToPolar(re, im, magnitude, phase);
    }

    void cartToPolar::operator()(buffer& re, Flt im) {
      const UInt length = Clamp(re.getLength());
      Fit(magnitudeOut, length);
      Fit(phaseOut, length);
      const Flt* a = re.getPtr();
      Flt* mag = magnitudeOut.getPtr();
      Flt* ph = phaseOut.getPtr();
      for (UInt i = 0; i < length; ++i)
        ToPolar(a[i], im, mag[i], ph[i]);
    }

    void cartToPolar::operator()(buffer& re, buffer& im) {
      const UInt length = Shorter(re, im);
      Fit(magnitudeOut, length);
      Fit(phaseOut, length);
      const Flt* a = re.getPtr();
      const Flt* b = im.getPtr();
      Flt* mag = magnitudeOut.getPtr();
      Flt* ph = phaseOut.getPtr();
      for (UInt i = 0; i < length; ++i)
        ToPolar(a[i], b[i], mag[i], ph[i]);
    }

    // ─── polarToCart ─────────────────────────────────────────────────────────

    polarToCart::polarToCart() : realOut(STANDARD_BUFFERSIZE), imaginaryOut(STANDARD_BUFFERSIZE) {}

    void polarToCart::process(Flt magnitude, Flt phase, Flt& re, Flt& im) {
      ToCart(magnitude, phase, re, im);
    }

    void polarToCart::operator()(buffer& magnitude, Flt phase) {
      const UInt length = Clamp(magnitude.getLength());
      Fit(realOut, length);
      Fit(imaginaryOut, length);
      // One angle for the whole block: its cosine and sine once.
      const Flt c = std::cos(phase);
      const Flt s = std::sin(phase);
      const Flt* m = magnitude.getPtr();
      Flt* re = realOut.getPtr();
      Flt* im = imaginaryOut.getPtr();
      for (UInt i = 0; i < length; ++i) {
        re[i] = Finite(m[i] * c);
        im[i] = Finite(m[i] * s);
      }
    }

    void polarToCart::operator()(buffer& magnitude, buffer& phase) {
      const UInt length = Shorter(magnitude, phase);
      Fit(realOut, length);
      Fit(imaginaryOut, length);
      const Flt* m = magnitude.getPtr();
      const Flt* p = phase.getPtr();
      Flt* re = realOut.getPtr();
      Flt* im = imaginaryOut.getPtr();
      for (UInt i = 0; i < length; ++i)
        ToCart(m[i], p[i], re[i], im[i]);
    }

    // ─── phaseWrap ───────────────────────────────────────────────────────────

    phaseWrap::phaseWrap() : output(STANDARD_BUFFERSIZE) {}

    Flt phaseWrap::process(Flt in) {
      return Wrap(in);
    }

    buffer& phaseWrap::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Wrap(a[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
