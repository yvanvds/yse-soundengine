#include "downsample.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

    downsample::downsample() : output(STANDARD_BUFFERSIZE) {}

    downsample& downsample::period(Flt value) {
      if (!std::isnan(value)) length = std::max(value, MIN_PERIOD);
      return *this;
    }

    downsample& downsample::reset() {
      count = std::numeric_limits<double>::infinity();
      hold = 0.f;
      return *this;
    }

    Flt downsample::Step(Flt in) {
      if (count >= length) {
        // Keep the fraction of a fractional period; a count still past the
        // period (it was lowered, or this is the first take) starts again.
        const double rest = count - length;
        count = rest < length ? rest : 0.0;
        if (std::isfinite(in)) hold = in;
      }
      count += 1.0;
      return hold;
    }

    Flt downsample::process(Flt in) {
      return Step(in);
    }

    buffer& downsample::operator()(buffer& in) {
      const UInt size = Clamp(in.getLength());
      Fit(output, size);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < size; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& downsample::operator()(buffer& in, buffer& periodSignal) {
      const UInt size = std::min(Clamp(in.getLength()), periodSignal.getLength());
      Fit(output, size);
      const Flt* x = in.getPtr();
      const Flt* p = periodSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < size; ++i) {
        period(p[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
