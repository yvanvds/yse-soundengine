#include "compare.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;
  using OP = YSE::DSP::compare::OP;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // One block of `op(in, right)` with a constant right operand. The resize
  // stays within the capacity set in the constructor: no allocation.
  template <class Op> buffer& Apply(buffer& output, buffer& in, Flt right, Op op) {
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
  template <class Op> buffer& Apply(buffer& output, buffer& in, buffer& right, Op op) {
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

  // `a` and `b` equal within `fuzzy`. The exact test comes first so equal
  // infinities compare equal; a NaN fails both.
  inline bool Near(Flt a, Flt b, Flt fuzzy) {
    return a == b || std::fabs(a - b) <= fuzzy;
  }

  inline Flt Truth(bool value) {
    return value ? 1.f : 0.f;
  }

  // Picks the operator once per block so the sample loop has no branch on it.
  template <class Right>
  buffer& Dispatch(OP op, Flt fuzzy, buffer& output, buffer& in, Right& right) {
    switch (op) {
    case OP::EQUAL:
      return Apply(output, in, right, [fuzzy](Flt a, Flt b) { return Truth(Near(a, b, fuzzy)); });
    case OP::NOT_EQUAL:
      return Apply(output, in, right, [fuzzy](Flt a, Flt b) { return Truth(!Near(a, b, fuzzy)); });
    case OP::GREATER:
      return Apply(output, in, right, [](Flt a, Flt b) { return Truth(a > b); });
    case OP::GREATER_EQUAL:
      return Apply(output, in, right,
                   [fuzzy](Flt a, Flt b) { return Truth(a >= b || b - a <= fuzzy); });
    case OP::LESS:
      return Apply(output, in, right, [](Flt a, Flt b) { return Truth(a < b); });
    case OP::LESS_EQUAL:
    default:
      return Apply(output, in, right,
                   [fuzzy](Flt a, Flt b) { return Truth(a <= b || a - b <= fuzzy); });
    }
  }

  inline Flt Smaller(Flt a, Flt b) {
    // std::fmin: a NaN on one side gives the other side.
    return std::fmin(a, b);
  }

  inline Flt Larger(Flt a, Flt b) {
    return std::fmax(a, b);
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── compare ─────────────────────────────────────────────────────────────

    compare::compare(OP op) : op(op), fuzzy(0.f), output(STANDARD_BUFFERSIZE) {}

    compare& compare::setOperator(OP value) {
      op = value;
      return *this;
    }

    compare& compare::setFuzzy(Flt tolerance) {
      // A NaN tolerance fails `> 0` too, so it becomes 0.
      fuzzy = tolerance > 0.f ? tolerance : 0.f;
      return *this;
    }

    Flt compare::process(OP op, Flt in, Flt right, Flt fuzzy) {
      if (!(fuzzy > 0.f)) fuzzy = 0.f;
      switch (op) {
      case OP::EQUAL:
        return Truth(Near(in, right, fuzzy));
      case OP::NOT_EQUAL:
        return Truth(!Near(in, right, fuzzy));
      case OP::GREATER:
        return Truth(in > right);
      case OP::GREATER_EQUAL:
        return Truth(in >= right || right - in <= fuzzy);
      case OP::LESS:
        return Truth(in < right);
      case OP::LESS_EQUAL:
      default:
        return Truth(in <= right || in - right <= fuzzy);
      }
    }

    buffer& compare::operator()(buffer& in, Flt right) {
      return Dispatch(op, fuzzy, output, in, right);
    }

    buffer& compare::operator()(buffer& in, buffer& right) {
      return Dispatch(op, fuzzy, output, in, right);
    }

    // ─── minimum ─────────────────────────────────────────────────────────────

    minimum::minimum() : output(STANDARD_BUFFERSIZE) {}

    Flt minimum::process(Flt in, Flt right) {
      return Smaller(in, right);
    }

    buffer& minimum::operator()(buffer& in, Flt right) {
      return Apply(output, in, right, &Smaller);
    }

    buffer& minimum::operator()(buffer& in, buffer& right) {
      return Apply(output, in, right, &Smaller);
    }

    // ─── maximum ─────────────────────────────────────────────────────────────

    maximum::maximum() : output(STANDARD_BUFFERSIZE) {}

    Flt maximum::process(Flt in, Flt right) {
      return Larger(in, right);
    }

    buffer& maximum::operator()(buffer& in, Flt right) {
      return Apply(output, in, right, &Larger);
    }

    buffer& maximum::operator()(buffer& in, buffer& right) {
      return Apply(output, in, right, &Larger);
    }

  } // namespace DSP
} // namespace YSE
