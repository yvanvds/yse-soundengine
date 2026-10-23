#include "bitwise.hpp"

#include <cstring>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  constexpr U32 kExponentMask = 0x7F800000u;

  inline U32 RawBits(Flt value) {
    U32 bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
  }

  // An all-ones exponent is NaN or infinity. Read from the bits, so the test
  // holds whatever floating-point flags the compiler was given.
  inline bool NonFinite(U32 bits) {
    return (bits & kExponentMask) == kExponentMask;
  }

  inline Flt RawFloat(U32 bits) {
    if (NonFinite(bits)) return 0.f;
    Flt value = 0.f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
  }

  // Toward zero, saturating at the Int range; NaN is 0. A plain cast of an
  // out-of-range float to int is undefined behaviour.
  inline Int ToInteger(Flt value) {
    if (value != value) return 0;
    if (value >= 2147483647.f) return 2147483647;
    if (value <= -2147483648.f) return -2147483647 - 1;
    return static_cast<Int>(value);
  }

  template <bool Integer> inline U32 Read(Flt value) {
    return Integer ? static_cast<U32>(ToInteger(value)) : RawBits(value);
  }

  template <bool Integer> inline Flt Write(U32 bits) {
    return Integer ? static_cast<Flt>(static_cast<Int>(bits)) : RawFloat(bits);
  }

  struct And {
    static U32 Apply(U32 a, U32 b) {
      return a & b;
    }
  };
  struct Or {
    static U32 Apply(U32 a, U32 b) {
      return a | b;
    }
  };
  struct Xor {
    static U32 Apply(U32 a, U32 b) {
      return a ^ b;
    }
  };

  // Calls f with the operation as a type, picked once per block so the sample
  // loop does not branch on it.
  template <class F> auto WithOp(YSE::DSP::bitwise::OP op, F&& f) {
    switch (op) {
    case YSE::DSP::bitwise::OP::OR:
      return f(Or{});
    case YSE::DSP::bitwise::OP::XOR:
      return f(Xor{});
    case YSE::DSP::bitwise::OP::AND:
    default:
      return f(And{});
    }
  }

  template <class Op, bool LeftInt> void MaskBlock(Flt* out, const Flt* in, U32 mask, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = Write<LeftInt>(Op::Apply(Read<LeftInt>(in[i]), mask));
  }

  template <class Op, bool LeftInt, bool RightInt>
  void SignalBlock(Flt* out, const Flt* in, const Flt* right, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = Write<LeftInt>(Op::Apply(Read<LeftInt>(in[i]), Read<RightInt>(right[i])));
  }

  // Integer mode: left doubles per bit (wrapping), right halves toward minus
  // infinity. Raw mode: a logical shift of the bit pattern. 32 bits or more
  // empty the word.
  template <bool Integer> inline U32 Shift(U32 bits, Int shift) {
    if (shift >= 0) return shift >= 32 ? 0u : bits << shift;
    const Int right = -shift; // at most 64: ClampShift ran first
    const bool negative = Integer && (bits & 0x80000000u) != 0;
    if (right >= 32) return negative ? 0xFFFFFFFFu : 0u;
    // Sign-extending without relying on >> of a negative int, which C++17
    // leaves implementation-defined.
    return negative ? ~(~bits >> right) : bits >> right;
  }

  template <bool Integer> void ShiftBlock(Flt* out, const Flt* in, Int shift, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = Write<Integer>(Shift<Integer>(Read<Integer>(in[i]), shift));
  }

  template <bool Integer> void NotBlock(Flt* out, const Flt* in, UInt length) {
    for (UInt i = 0; i < length; ++i)
      out[i] = Write<Integer>(~Read<Integer>(in[i]));
  }

  inline Int ClampShift(Int value) {
    // -INT_MIN overflows; nothing past 32 bits differs anyway.
    return value < -64 ? -64 : (value > 64 ? 64 : value);
  }
} // namespace

namespace YSE {
  namespace DSP {

    BIT_MODE BitModeFromInt(Int value) {
      switch (value) {
      case 1:
        return BIT_MODE::INTEGER;
      case 2:
        return BIT_MODE::RAW_INTEGER;
      case 3:
        return BIT_MODE::INTEGER_RAW;
      default:
        return BIT_MODE::RAW;
      }
    }

    bool BitLeftIsInteger(BIT_MODE mode) {
      return mode == BIT_MODE::INTEGER || mode == BIT_MODE::INTEGER_RAW;
    }

    bool BitRightIsInteger(BIT_MODE mode) {
      return mode == BIT_MODE::INTEGER || mode == BIT_MODE::RAW_INTEGER;
    }

    Int FloatToBits(Flt value, bool integer) {
      return static_cast<Int>(integer ? Read<true>(value) : Read<false>(value));
    }

    Flt BitsToFloat(Int bits, bool integer) {
      const U32 u = static_cast<U32>(bits);
      return integer ? Write<true>(u) : Write<false>(u);
    }

    // ─── bitwise ─────────────────────────────────────────────────────────────

    bitwise::bitwise(OP opValue, Int maskValue, BIT_MODE modeValue)
      : op(opValue), mask(maskValue), mode(modeValue), output(STANDARD_BUFFERSIZE) {}

    bitwise& bitwise::setOp(OP value) {
      op = value;
      return *this;
    }

    bitwise& bitwise::setMask(Int value) {
      mask = value;
      return *this;
    }

    bitwise& bitwise::setMode(BIT_MODE value) {
      mode = value;
      return *this;
    }

    Flt bitwise::process(OP op, BIT_MODE mode, Flt left, Int rightBits) {
      const bool leftInt = BitLeftIsInteger(mode);
      const U32 a = static_cast<U32>(FloatToBits(left, leftInt));
      const U32 b = static_cast<U32>(rightBits);
      return WithOp(op, [&](auto o) {
        return BitsToFloat(static_cast<Int>(decltype(o)::Apply(a, b)), leftInt);
      });
    }

    buffer& bitwise::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const U32 m = static_cast<U32>(mask.load());
      const bool leftInt = BitLeftIsInteger(mode);
      Flt* out = output.getPtr();
      const Flt* a = in.getPtr();
      WithOp(op, [&](auto o) {
        using Op = decltype(o);
        if (leftInt)
          MaskBlock<Op, true>(out, a, m, length);
        else
          MaskBlock<Op, false>(out, a, m, length);
        return 0;
      });
      return output;
    }

    buffer& bitwise::operator()(buffer& in, buffer& right) {
      const UInt shorter = in.getLength() < right.getLength() ? in.getLength() : right.getLength();
      const UInt length = Clamp(shorter);
      if (output.getLength() != length) output.resize(length);
      const BIT_MODE m = mode;
      Flt* out = output.getPtr();
      const Flt* a = in.getPtr();
      const Flt* b = right.getPtr();
      WithOp(op, [&](auto o) {
        using Op = decltype(o);
        switch (m) {
        case BIT_MODE::INTEGER:
          SignalBlock<Op, true, true>(out, a, b, length);
          break;
        case BIT_MODE::RAW_INTEGER:
          SignalBlock<Op, false, true>(out, a, b, length);
          break;
        case BIT_MODE::INTEGER_RAW:
          SignalBlock<Op, true, false>(out, a, b, length);
          break;
        case BIT_MODE::RAW:
        default:
          SignalBlock<Op, false, false>(out, a, b, length);
          break;
        }
        return 0;
      });
      return output;
    }

    // ─── bitnot ──────────────────────────────────────────────────────────────

    bitnot::bitnot(BIT_MODE modeValue) : mode(modeValue), output(STANDARD_BUFFERSIZE) {}

    bitnot& bitnot::setMode(BIT_MODE value) {
      mode = value;
      return *this;
    }

    Flt bitnot::process(BIT_MODE mode, Flt in) {
      const bool integer = BitLeftIsInteger(mode);
      return BitsToFloat(~FloatToBits(in, integer), integer);
    }

    buffer& bitnot::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      if (output.getLength() != length) output.resize(length);
      if (BitLeftIsInteger(mode))
        NotBlock<true>(output.getPtr(), in.getPtr(), length);
      else
        NotBlock<false>(output.getPtr(), in.getPtr(), length);
      return output;
    }

    // ─── bitshift ────────────────────────────────────────────────────────────

    bitshift::bitshift(Int shiftValue, BIT_MODE modeValue)
      : shift(ClampShift(shiftValue)), mode(modeValue), output(STANDARD_BUFFERSIZE) {}

    bitshift& bitshift::setShift(Int value) {
      shift = ClampShift(value);
      return *this;
    }

    bitshift& bitshift::setMode(BIT_MODE value) {
      mode = value;
      return *this;
    }

    Flt bitshift::process(BIT_MODE mode, Int shift, Flt in) {
      const Int s = ClampShift(shift);
      if (BitLeftIsInteger(mode)) return Write<true>(Shift<true>(Read<true>(in), s));
      return Write<false>(Shift<false>(Read<false>(in), s));
    }

    buffer& bitshift::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      if (output.getLength() != length) output.resize(length);
      const Int s = shift;
      if (BitLeftIsInteger(mode))
        ShiftBlock<true>(output.getPtr(), in.getPtr(), s, length);
      else
        ShiftBlock<false>(output.getPtr(), in.getPtr(), s, length);
      return output;
    }

    // ─── bitsafe ─────────────────────────────────────────────────────────────

    bitsafe::bitsafe() : output(STANDARD_BUFFERSIZE) {}

    Flt bitsafe::process(Flt in) {
      return NonFinite(RawBits(in)) ? 0.f : in;
    }

    void bitsafe::apply(buffer& io) {
      Flt* p = io.getPtr();
      const UInt length = io.getLength();
      for (UInt i = 0; i < length; ++i)
        p[i] = process(p[i]);
    }

    buffer& bitsafe::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      if (output.getLength() != length) output.resize(length);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = process(a[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
