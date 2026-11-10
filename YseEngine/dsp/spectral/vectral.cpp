#include "vectral.hpp"

#include <algorithm>
#include <cmath>
#include "../filters/slewLimiter.hpp"
#include "../filters/slide.hpp"

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline UInt Capacity(UInt values) {
    if (values < 1) return 1;
    return values > YSE::DSP::vectral::MAXIMUM_SIZE ? YSE::DSP::vectral::MAXIMUM_SIZE : values;
  }

  // After a slide or deltaclip step, the ramp state rests on the value
  // reached, so a later rampsmooth starts from it: the next input that
  // differs starts a ramp, one that equals it changes nothing.
  inline void Rest(YSE::DSP::rampSmooth::state& s) {
    s.target = static_cast<Flt>(s.current);
    s.remaining = 0;
    s.increment = 0.0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    vectral::vectral(UInt capacity)
      : output(STANDARD_BUFFERSIZE), frame(Capacity(capacity)), used(Capacity(capacity)) {}

    vectral& vectral::size(UInt values) {
      const UInt next = std::min(std::max<UInt>(values, 1), capacity());
      if (next != used) {
        used = next;
        clear();
      }
      return *this;
    }

    vectral& vectral::rampsmooth(Flt up, Flt down) {
      current = RAMPSMOOTH;
      if (std::isfinite(up)) rampUpFrames = up;
      if (std::isfinite(down)) rampDownFrames = down;
      return *this;
    }

    vectral& vectral::slide(Flt up, Flt down) {
      current = SLIDE;
      if (std::isfinite(up)) slideUpAmount = up;
      if (std::isfinite(down)) slideDownAmount = down;
      return *this;
    }

    vectral& vectral::deltaclip(Flt lowest, Flt highest) {
      current = DELTACLIP;
      if (!std::isnan(lowest)) clipLow = lowest;
      if (!std::isnan(highest)) clipHigh = highest;
      return *this;
    }

    vectral& vectral::clear() {
      std::fill(frame.begin(), frame.end(), rampSmooth::state{});
      return *this;
    }

    Flt vectral::value(UInt index) const {
      return index < used ? static_cast<Flt>(frame[index].current) : 0.f;
    }

    UInt vectral::Index(Flt position) const {
      // A NaN fails the comparison too.
      if (!(position >= 0.f) || position >= static_cast<Flt>(used)) return used;
      return static_cast<UInt>(position);
    }

    Flt vectral::process(Flt outputIndex, Flt inputIndex, Flt in) {
      const UInt at = Index(inputIndex);
      if (at < used) {
        rampSmooth::state& s = frame[at];
        switch (current) {
        case RAMPSMOOTH:
          rampSmooth::Advance(s, in, rampUpFrames, rampDownFrames);
          break;
        case SLIDE:
          s.current = slide::Advance(s.current, in, slideUpAmount, slideDownAmount);
          Rest(s);
          break;
        case DELTACLIP:
          s.current = static_cast<Dbl>(
              slewLimiter::Advance(static_cast<Flt>(s.current), in, clipLow, clipHigh));
          Rest(s);
          break;
        }
      }
      const UInt from = Index(outputIndex);
      return from < used ? static_cast<Flt>(frame[from].current) : 0.f;
    }

    buffer& vectral::operator()(buffer& outputIndex, buffer& inputIndex, buffer& in) {
      const UInt length =
          std::min({Clamp(in.getLength()), outputIndex.getLength(), inputIndex.getLength()});
      Fit(output, length);
      const Flt* o = outputIndex.getPtr();
      const Flt* i = inputIndex.getPtr();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt n = 0; n < length; ++n)
        y[n] = process(o[n], i[n], x[n]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
