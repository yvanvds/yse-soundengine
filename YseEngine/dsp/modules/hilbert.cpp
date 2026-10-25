/*
  ==============================================================================

    hilbert.cpp
    Created: 31 Jan 2014 2:56:08pm
    Author:  yvan

  ==============================================================================
*/

#include "hilbert.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    hilbert::hilbert() : re(STANDARD_BUFFERSIZE), im(STANDARD_BUFFERSIZE) {}

    hilbert& hilbert::clear() {
      for (state& s : realState)
        s = state();
      for (state& s : imaginaryState)
        s = state();
      last = outputs();
      return *this;
    }

    // An allpass section: the numerator is the denominator reversed.
    Dbl hilbert::Run(const section& c, state& s, Dbl x) {
      const Dbl y = c.a2 * x + s.s1;
      s.s1 = c.a1 * x - c.a1 * y + s.s2;
      s.s2 = x - c.a2 * y;
      return y;
    }

    void hilbert::Step(Flt in) {
      if (!std::isfinite(in)) return;
      const Dbl x = static_cast<Dbl>(in);
      const Dbl r = Run(REAL_SECTIONS[1], realState[1], Run(REAL_SECTIONS[0], realState[0], x));
      const Dbl i = Run(IMAGINARY_SECTIONS[1], imaginaryState[1],
                        Run(IMAGINARY_SECTIONS[0], imaginaryState[0], x));
      last.real = static_cast<Flt>(r);
      last.imaginary = static_cast<Flt>(i);
      // An output beyond a float restarts the filter instead of latching.
      if (!std::isfinite(last.real) || !std::isfinite(last.imaginary)) clear();
    }

    hilbert::outputs hilbert::process(Flt in) {
      Step(in);
      return last;
    }

    buffer& hilbert::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (re.getLength() != length) re.resize(length);
      if (im.getLength() != length) im.resize(length);
      const Flt* x = in.getPtr();
      Flt* r = re.getPtr();
      Flt* i = im.getPtr();
      for (UInt n = 0; n < length; ++n) {
        Step(x[n]);
        r[n] = last.real;
        i[n] = last.imaginary;
      }
      return re;
    }

    void hilbert::operator()(buffer& in, buffer& out1, buffer& out2) {
      (*this)(in);
      out1 = re;
      out2 = im;
    }

  } // namespace DSP
} // namespace YSE
