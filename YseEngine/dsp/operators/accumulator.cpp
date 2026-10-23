#include "accumulator.hpp"

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    accumulator::accumulator(Dbl initial) : total(initial), output(STANDARD_BUFFERSIZE) {}

    Flt accumulator::process(Flt in) {
      total += static_cast<Dbl>(in);
      return static_cast<Flt>(total);
    }

    Flt accumulator::process(Flt in, Flt reset) {
      if (reset != 0.f) total = 0.0;
      total += static_cast<Dbl>(in);
      return static_cast<Flt>(total);
    }

    buffer& accumulator::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      Dbl s = total;
      for (UInt i = 0; i < length; ++i) {
        s += static_cast<Dbl>(a[i]);
        out[i] = static_cast<Flt>(s);
      }
      total = s;
      return output;
    }

    buffer& accumulator::operator()(buffer& in, buffer& reset) {
      const UInt shorter = in.getLength() < reset.getLength() ? in.getLength() : reset.getLength();
      const UInt length = Clamp(shorter);
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      const Flt* r = reset.getPtr();
      Flt* out = output.getPtr();
      Dbl s = total;
      for (UInt i = 0; i < length; ++i) {
        if (r[i] != 0.f) s = 0.0;
        s += static_cast<Dbl>(a[i]);
        out[i] = static_cast<Flt>(s);
      }
      total = s;
      return output;
    }

    accumulator& accumulator::set(Dbl value) {
      total = value;
      return *this;
    }

    accumulator& accumulator::reset() {
      total = 0.0;
      return *this;
    }

    Dbl accumulator::sum() const {
      return total;
    }

  } // namespace DSP
} // namespace YSE
