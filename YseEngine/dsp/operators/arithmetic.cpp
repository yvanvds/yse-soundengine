#include "arithmetic.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // One block of `op(in, right)` with a constant right operand. The resize
  // stays within the capacity set in the constructor: no allocation.
  template <Flt (*op)(Flt, Flt)>
  YSE::DSP::buffer& Apply(YSE::DSP::buffer& output, YSE::DSP::buffer& in, Flt right) {
    const UInt length = Clamp(in.getLength());
    if (output.getLength() != length) output.resize(length);
    const Flt* a = in.getPtr();
    Flt* out = output.getPtr();
    for (UInt i = 0; i < length; ++i)
      out[i] = op(a[i], right);
    return output;
  }

  // One block of `op(in, right)` sample by sample, as long as the shorter
  // input.
  template <Flt (*op)(Flt, Flt)>
  YSE::DSP::buffer& Apply(YSE::DSP::buffer& output, YSE::DSP::buffer& in, YSE::DSP::buffer& right) {
    const UInt shorter = in.getLength() < right.getLength() ? in.getLength() : right.getLength();
    const UInt length = Clamp(shorter);
    if (output.getLength() != length) output.resize(length);
    const Flt* a = in.getPtr();
    const Flt* b = right.getPtr();
    Flt* out = output.getPtr();
    for (UInt i = 0; i < length; ++i)
      out[i] = op(a[i], b[i]);
    return output;
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── reverseSubtract ─────────────────────────────────────────────────────

    reverseSubtract::reverseSubtract() : output(STANDARD_BUFFERSIZE) {}

    Flt reverseSubtract::process(Flt in, Flt right) {
      return right - in;
    }

    buffer& reverseSubtract::operator()(buffer& in, Flt right) {
      return Apply<&reverseSubtract::process>(output, in, right);
    }

    buffer& reverseSubtract::operator()(buffer& in, buffer& right) {
      return Apply<&reverseSubtract::process>(output, in, right);
    }

    // ─── reverseDivide ───────────────────────────────────────────────────────

    reverseDivide::reverseDivide() : output(STANDARD_BUFFERSIZE) {}

    Flt reverseDivide::process(Flt in, Flt right) {
      return in != 0.f ? right / in : 0.f;
    }

    buffer& reverseDivide::operator()(buffer& in, Flt right) {
      return Apply<&reverseDivide::process>(output, in, right);
    }

    buffer& reverseDivide::operator()(buffer& in, buffer& right) {
      return Apply<&reverseDivide::process>(output, in, right);
    }

    // ─── modulo ──────────────────────────────────────────────────────────────

    modulo::modulo() : output(STANDARD_BUFFERSIZE) {}

    Flt modulo::process(Flt in, Flt divisor) {
      return divisor != 0.f ? std::fmod(in, divisor) : 0.f;
    }

    buffer& modulo::operator()(buffer& in, Flt divisor) {
      return Apply<&modulo::process>(output, in, divisor);
    }

    buffer& modulo::operator()(buffer& in, buffer& divisor) {
      return Apply<&modulo::process>(output, in, divisor);
    }

  } // namespace DSP
} // namespace YSE
