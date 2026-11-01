#include "direction.hpp"

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // 1 above, -1 below, 0 equal or unordered (NaN on either side).
  inline Flt Sign(Flt now, Flt before) {
    return static_cast<Flt>(static_cast<int>(now > before) - static_cast<int>(now < before));
  }
} // namespace

namespace YSE {
  namespace DSP {

    direction::direction() : last(0.f), output(STANDARD_BUFFERSIZE) {}

    Flt direction::process(Flt in) {
      const Flt result = Sign(in, last);
      last = in;
      return result;
    }

    buffer& direction::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      Flt before = last;
      for (UInt i = 0; i < length; ++i) {
        const Flt x = a[i];
        out[i] = Sign(x, before);
        before = x;
      }
      last = before;
      return output;
    }

    direction& direction::set(Flt value) {
      last = value;
      return *this;
    }

    direction& direction::reset() {
      last = 0.f;
      return *this;
    }

    Flt direction::previous() const {
      return last;
    }

  } // namespace DSP
} // namespace YSE
