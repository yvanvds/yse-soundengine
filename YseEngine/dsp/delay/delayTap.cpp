#include "delayTap.hpp"

#include <algorithm>
#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    delayTap::delayTap() : output(STANDARD_BUFFERSIZE) {}

    buffer& delayTap::operator()(const delayLine& line, Flt delayMs, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);

      const Dbl wanted =
          std::round(static_cast<Dbl>(delayMs) * 0.001 * static_cast<Dbl>(SAMPLERATE));
      const auto longest = static_cast<Dbl>(line.longestDelay(length));
      const auto delay =
          static_cast<Int>(std::min(std::max(wanted, static_cast<Dbl>(length)), longest));

      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        out[i] = line.at(static_cast<Int>(i) - delay);
      }
      return output;
    }

    buffer& delayTap::operator()(const delayLine& line, buffer& delayMs) {
      const UInt length = Clamp(delayMs.getLength());
      if (output.getLength() != length) output.resize(length);

      const Dbl toSamples = 0.001 * static_cast<Dbl>(SAMPLERATE);
      // The interpolator reads one sample past the point, so the shortest
      // delay is two samples over a block: everything it reads predates the
      // block origin.
      const auto shortest = static_cast<Dbl>(length + 2);
      const auto longest = static_cast<Dbl>(line.longestDelay(length));
      const Flt* in = delayMs.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        const Dbl delay =
            std::min(std::max(static_cast<Dbl>(in[i]) * toSamples, shortest), longest);
        // The read point, as an offset from the block origin.
        const Dbl point = static_cast<Dbl>(i) - delay;
        const Dbl whole = std::floor(point);
        const auto frac = static_cast<Flt>(point - whole);
        const auto p = static_cast<Int>(whole);
        const Flt y0 = line.at(p - 1);
        const Flt y1 = line.at(p);
        const Flt y2 = line.at(p + 1);
        const Flt y3 = line.at(p + 2);
        // Catmull-Rom: passes through y1 at frac 0 and y2 at frac 1.
        const Flt c1 = 0.5f * (y2 - y0);
        const Flt c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const Flt c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        out[i] = ((c3 * frac + c2) * frac + c1) * frac + y1;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
