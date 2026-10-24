#include "kink.hpp"

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

  inline Flt Bend(Flt x, Flt s) {
    if (!std::isfinite(x)) return 0.f;
    if (!std::isfinite(s)) s = 1.f;
    const Flt rise = x * s;
    // Below the knee, or no knee inside the ramp (s <= 0.5, where the second
    // line would divide by s - 0.5 <= 0): the first line.
    Flt y = rise;
    if (rise >= 0.5f && s > 0.5f) y = 0.5f + 0.5f * (rise - 0.5f) / (s - 0.5f);
    return std::isfinite(y) ? y : 0.f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    kink::kink() : output(STANDARD_BUFFERSIZE) {}

    Flt kink::process(Flt phase, Flt slope) {
      return Bend(phase, slope);
    }

    buffer& kink::operator()(buffer& phase, Flt slope) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Bend(in[i], slope);
      return output;
    }

    buffer& kink::operator()(buffer& phase, buffer& slope) {
      const UInt length =
          Clamp(phase.getLength() < slope.getLength() ? phase.getLength() : slope.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      const Flt* s = slope.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Bend(in[i], s[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
