#include "zeroCrossing.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // 1 above 0, -1 below, 0 for zero and NaN (on neither side).
  inline Int Side(Flt x) {
    return static_cast<Int>(x > 0.f) - static_cast<Int>(x < 0.f);
  }
} // namespace

namespace YSE {
  namespace DSP {

    zeroCrossing::zeroCrossing()
      : side(0),
        clickVolume(1.f),
        count(0),
        pos{},
        countOut(STANDARD_BUFFERSIZE),
        clickOut(STANDARD_BUFFERSIZE) {}

    bool zeroCrossing::process(Flt in) {
      const Int now = Side(in);
      if (now == 0) return false;
      const bool crossed = side != 0 && now != side;
      side = now;
      return crossed;
    }

    UInt zeroCrossing::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (countOut.getLength() != length) countOut.resize(length);
      if (clickOut.getLength() != length) clickOut.resize(length);
      const Flt* a = in.getPtr();
      Flt* click = clickOut.getPtr();
      const Flt v = clickVolume;
      Int before = side;
      UInt c = 0;
      for (UInt i = 0; i < length; ++i) {
        const Int now = Side(a[i]);
        const bool crossed = now != 0 && before != 0 && now != before;
        if (now != 0) before = now;
        click[i] = crossed ? v : 0.f;
        if (crossed) pos[c++] = i;
      }
      side = before;
      count = c;
      Flt* n = countOut.getPtr();
      const auto total = static_cast<Flt>(c);
      for (UInt i = 0; i < length; ++i)
        n[i] = total;
      return c;
    }

    buffer& zeroCrossing::counts() {
      return countOut;
    }

    buffer& zeroCrossing::clicks() {
      return clickOut;
    }

    UInt zeroCrossing::crossings() const {
      return count;
    }

    UInt zeroCrossing::crossing(UInt i) const {
      return pos[i];
    }

    zeroCrossing& zeroCrossing::volume(Flt value) {
      clickVolume = std::isnan(value) ? 0.f : value;
      return *this;
    }

    Flt zeroCrossing::volume() const {
      return clickVolume;
    }

    zeroCrossing& zeroCrossing::reset() {
      side = 0;
      count = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
