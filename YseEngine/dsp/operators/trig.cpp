#include "trig.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;
  using FN = YSE::DSP::trig::FN;

  // The widest argument whose cosh / sinh still fits a float (cosh(89.5)
  // overflows).
  constexpr Flt kHyperbolicEdge = 88.f;
  // The largest float below 1: atanh of it is finite (about 8.66).
  constexpr Flt kAtanhEdge = 0.99999994f;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Clamps to lo..hi. A NaN passes through untouched, for Finite() to catch.
  inline Flt Bound(Flt x, Flt lo, Flt hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
  }

  inline Flt Finite(Flt x) {
    return std::isfinite(x) ? x : 0.f;
  }

  template <class Op> buffer& Apply(buffer& output, buffer& in, Op op) {
    const UInt length = Clamp(in.getLength());
    // Within the capacity set in the constructor: no allocation.
    if (output.getLength() != length) output.resize(length);
    const Flt* a = in.getPtr();
    Flt* out = output.getPtr();
    for (UInt i = 0; i < length; ++i)
      out[i] = Finite(op(a[i]));
    return output;
  }

  inline Flt Cos(Flt x) {
    return std::cos(x);
  }
  inline Flt Sin(Flt x) {
    return std::sin(x);
  }
  inline Flt Tan(Flt x) {
    return std::tan(x);
  }
  inline Flt Acos(Flt x) {
    return std::acos(Bound(x, -1.f, 1.f));
  }
  inline Flt Asin(Flt x) {
    return std::asin(Bound(x, -1.f, 1.f));
  }
  inline Flt Atan(Flt x) {
    return std::atan(x);
  }
  inline Flt Cosh(Flt x) {
    return std::cosh(Bound(x, -kHyperbolicEdge, kHyperbolicEdge));
  }
  inline Flt Sinh(Flt x) {
    return std::sinh(Bound(x, -kHyperbolicEdge, kHyperbolicEdge));
  }
  inline Flt Tanh(Flt x) {
    return std::tanh(x);
  }
  inline Flt Acosh(Flt x) {
    return std::acosh(x < 1.f ? 1.f : x);
  }
  inline Flt Asinh(Flt x) {
    return std::asinh(x);
  }
  inline Flt Atanh(Flt x) {
    return std::atanh(Bound(x, -kAtanhEdge, kAtanhEdge));
  }

  // The function for `fn`, for the one-sample path.
  Flt (*Function(FN fn))(Flt) {
    switch (fn) {
    case FN::COS:
      return &Cos;
    case FN::SIN:
      return &Sin;
    case FN::TAN:
      return &Tan;
    case FN::ACOS:
      return &Acos;
    case FN::ASIN:
      return &Asin;
    case FN::ATAN:
      return &Atan;
    case FN::COSH:
      return &Cosh;
    case FN::SINH:
      return &Sinh;
    case FN::TANH:
      return &Tanh;
    case FN::ACOSH:
      return &Acosh;
    case FN::ASINH:
      return &Asinh;
    case FN::ATANH:
    default:
      return &Atanh;
    }
  }

  // One block of `fn`. The function is picked once per block and each case
  // passes a lambda, so the sample loop is a direct, inlinable call.
  buffer& Dispatch(FN fn, buffer& output, buffer& in) {
    switch (fn) {
    case FN::COS:
      return Apply(output, in, [](Flt x) { return Cos(x); });
    case FN::SIN:
      return Apply(output, in, [](Flt x) { return Sin(x); });
    case FN::TAN:
      return Apply(output, in, [](Flt x) { return Tan(x); });
    case FN::ACOS:
      return Apply(output, in, [](Flt x) { return Acos(x); });
    case FN::ASIN:
      return Apply(output, in, [](Flt x) { return Asin(x); });
    case FN::ATAN:
      return Apply(output, in, [](Flt x) { return Atan(x); });
    case FN::COSH:
      return Apply(output, in, [](Flt x) { return Cosh(x); });
    case FN::SINH:
      return Apply(output, in, [](Flt x) { return Sinh(x); });
    case FN::TANH:
      return Apply(output, in, [](Flt x) { return Tanh(x); });
    case FN::ACOSH:
      return Apply(output, in, [](Flt x) { return Acosh(x); });
    case FN::ASINH:
      return Apply(output, in, [](Flt x) { return Asinh(x); });
    case FN::ATANH:
    default:
      return Apply(output, in, [](Flt x) { return Atanh(x); });
    }
  }

  inline Flt Atan2(Flt y, Flt x) {
    return Finite(std::atan2(y, x));
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── trig ────────────────────────────────────────────────────────────────

    trig::trig(FN fn) : fn(fn), output(STANDARD_BUFFERSIZE) {}

    trig& trig::setFunction(FN value) {
      fn = value;
      return *this;
    }

    Flt trig::process(FN fn, Flt in) {
      return Finite(Function(fn)(in));
    }

    buffer& trig::operator()(buffer& in) {
      return Dispatch(fn, output, in);
    }

    // ─── arcTangent2 ─────────────────────────────────────────────────────────

    arcTangent2::arcTangent2() : output(STANDARD_BUFFERSIZE) {}

    Flt arcTangent2::process(Flt y, Flt x) {
      return Atan2(y, x);
    }

    buffer& arcTangent2::operator()(buffer& y, Flt x) {
      const UInt length = Clamp(y.getLength());
      if (output.getLength() != length) output.resize(length);
      const Flt* a = y.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Atan2(a[i], x);
      return output;
    }

    buffer& arcTangent2::operator()(buffer& y, buffer& x) {
      const UInt shorter = y.getLength() < x.getLength() ? y.getLength() : x.getLength();
      const UInt length = Clamp(shorter);
      if (output.getLength() != length) output.resize(length);
      const Flt* a = y.getPtr();
      const Flt* b = x.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Atan2(a[i], b[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
