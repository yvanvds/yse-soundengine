#include "edgeDetector.hpp"

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    edgeDetector::edgeDetector() : state(false), riseCount(0), fallCount(0), risePos{}, fallPos{} {}

    Int edgeDetector::process(Flt in) {
      const bool now = in != 0.f; // NaN != 0 holds: not a number is non-zero
      const Int result = static_cast<Int>(now && !state) - static_cast<Int>(state && !now);
      state = now;
      return result;
    }

    UInt edgeDetector::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      const Flt* a = in.getPtr();
      bool before = state;
      UInt r = 0;
      UInt f = 0;
      for (UInt i = 0; i < length; ++i) {
        const bool now = a[i] != 0.f;
        if (now != before) {
          if (now)
            risePos[r++] = i;
          else
            fallPos[f++] = i;
          before = now;
        }
      }
      state = before;
      riseCount = r;
      fallCount = f;
      return r + f;
    }

    UInt edgeDetector::rises() const {
      return riseCount;
    }

    UInt edgeDetector::falls() const {
      return fallCount;
    }

    UInt edgeDetector::rise(UInt i) const {
      return risePos[i];
    }

    UInt edgeDetector::fall(UInt i) const {
      return fallPos[i];
    }

    bool edgeDetector::high() const {
      return state;
    }

    edgeDetector& edgeDetector::set(bool nonZero) {
      state = nonZero;
      riseCount = 0;
      fallCount = 0;
      return *this;
    }

    edgeDetector& edgeDetector::reset() {
      return set(false);
    }

  } // namespace DSP
} // namespace YSE
