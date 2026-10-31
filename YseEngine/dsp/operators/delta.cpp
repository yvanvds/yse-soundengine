#include "delta.hpp"

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    delta::delta() : last(0.f), output(STANDARD_BUFFERSIZE) {}

    Flt delta::process(Flt in) {
      const Flt result = in - last;
      last = in;
      return result;
    }

    buffer& delta::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      Flt before = last;
      for (UInt i = 0; i < length; ++i) {
        const Flt x = a[i];
        out[i] = x - before;
        before = x;
      }
      last = before;
      return output;
    }

    delta& delta::set(Flt value) {
      last = value;
      return *this;
    }

    delta& delta::reset() {
      last = 0.f;
      return *this;
    }

    Flt delta::previous() const {
      return last;
    }

  } // namespace DSP
} // namespace YSE
