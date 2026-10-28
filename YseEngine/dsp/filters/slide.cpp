#include "slide.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;

  // Within one part in a million of the input (never finer than 1e-6
  // absolute) the output is the input: see the class documentation.
  constexpr double SNAP_TOLERANCE = 1e-6;

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

    slide::slide() : output(STANDARD_BUFFERSIZE) {}

    slide& slide::up(Flt amount) {
      if (std::isfinite(amount)) slideUp = amount;
      return *this;
    }

    slide& slide::down(Flt amount) {
      if (std::isfinite(amount)) slideDown = amount;
      return *this;
    }

    slide& slide::reset() {
      current = 0.0;
      return *this;
    }

    Flt slide::Effective(Flt amount) {
      // A NaN fails the comparison and reads as 1 too.
      return amount > 1.f ? amount : 1.f;
    }

    Flt slide::Step(Flt in) {
      if (std::isfinite(in)) {
        const Dbl target = static_cast<Dbl>(in);
        const Dbl amount = static_cast<Dbl>(Effective(target > current ? slideUp : slideDown));
        // The amount is at least 1, so the step never passes the input.
        Dbl next = current + (target - current) / amount;
        if (std::fabs(target - next) <= SNAP_TOLERANCE * std::max(1.0, std::fabs(target)))
          next = target;
        current = next;
      }
      return static_cast<Flt>(current);
    }

    Flt slide::process(Flt in) {
      return Step(in);
    }

    buffer& slide::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& slide::operator()(buffer& in, buffer& upSignal, buffer& downSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), upSignal.getLength(), downSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* u = upSignal.getPtr();
      const Flt* d = downSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        up(u[i]);
        down(d[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
