#include "rounding.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // A non-finite input gives 0. A step of 0 passes the sample through, and so
  // does a step whose quotient or product leaves the float range (a tiny or an
  // infinite step, a NaN step): the sample is already as fine as that step.
  template <bool Nearest> inline Flt RoundTo(Flt x, Flt step) {
    if (!std::isfinite(x)) return 0.f;
    if (step == 0.f) return x;
    const Flt s = std::fabs(step);
    const Flt q = x / s;
    const Flt r = (Nearest ? std::round(q) : std::floor(q)) * s;
    return std::isfinite(r) ? r : x;
  }

  inline Flt Trunc(Flt x) {
    return std::isfinite(x) ? std::trunc(x) : 0.f;
  }

  // The mode is picked once per block, so the sample loop is branch-free on it.
  template <bool Nearest> void Block(Flt* out, const Flt* in, Flt step, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = RoundTo<Nearest>(in[i], step);
  }

  template <bool Nearest> void Block(Flt* out, const Flt* in, const Flt* step, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = RoundTo<Nearest>(in[i], step[i]);
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── round ───────────────────────────────────────────────────────────────

    round::round(Flt stepValue, bool nearestValue)
      : step(stepValue), nearest(nearestValue), output(STANDARD_BUFFERSIZE) {}

    round& round::setStep(Flt value) {
      step = value;
      return *this;
    }

    round& round::setNearest(bool value) {
      nearest = value;
      return *this;
    }

    Flt round::process(Flt in, Flt step, bool nearest) {
      return nearest ? RoundTo<true>(in, step) : RoundTo<false>(in, step);
    }

    buffer& round::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const Flt s = step;
      if (nearest)
        Block<true>(output.getPtr(), in.getPtr(), s, length);
      else
        Block<false>(output.getPtr(), in.getPtr(), s, length);
      return output;
    }

    buffer& round::operator()(buffer& in, buffer& stepIn) {
      const UInt shorter =
          in.getLength() < stepIn.getLength() ? in.getLength() : stepIn.getLength();
      const UInt length = Clamp(shorter);
      if (output.getLength() != length) output.resize(length);
      if (nearest)
        Block<true>(output.getPtr(), in.getPtr(), stepIn.getPtr(), length);
      else
        Block<false>(output.getPtr(), in.getPtr(), stepIn.getPtr(), length);
      return output;
    }

    // ─── trunc ───────────────────────────────────────────────────────────────

    trunc::trunc() : output(STANDARD_BUFFERSIZE) {}

    Flt trunc::process(Flt in) {
      return Trunc(in);
    }

    buffer& trunc::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Trunc(a[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
