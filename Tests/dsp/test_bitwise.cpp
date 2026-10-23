// Tests for YSE::DSP::bitwise, bitnot, bitshift and bitsafe
// (YseEngine/dsp/operators/bitwise.hpp), issue #1051. Reference output is
// closed form: the same bit operation done on the IEEE 754 bit pattern (or on
// the integer) by hand, sample by sample.

#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "dsp/operators/bitwise.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::BIT_MODE;

namespace {

  std::uint32_t Bits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
  }

  float FromBits(std::uint32_t u) {
    float f = 0.f;
    std::memcpy(&f, &u, sizeof f);
    return f;
  }

  // -8 to just under 8 across one block, with fractions.
  void FillRamp(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (static_cast<float>(i) - 64.f) / 8.f + 0.3f;
  }

  bool AllFinite(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (!std::isfinite(b.getPtr()[i])) return false;
    return true;
  }

  const float kInf = std::numeric_limits<float>::infinity();
  const float kNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("bitwise: the conversion helpers read raw bits or integers (#1051)") {
    CHECK(YSE::DSP::FloatToBits(1.f, false) == 0x3F800000);
    CHECK(YSE::DSP::FloatToBits(-2.f, false) == static_cast<int>(0xC0000000u));
    CHECK(YSE::DSP::FloatToBits(1.7f, true) == 1);
    CHECK(YSE::DSP::FloatToBits(-1.7f, true) == -1);
    CHECK(YSE::DSP::FloatToBits(1e20f, true) == 2147483647); // saturates
    CHECK(YSE::DSP::FloatToBits(-1e20f, true) == -2147483647 - 1);
    CHECK(YSE::DSP::FloatToBits(kNaN, true) == 0);
    CHECK(YSE::DSP::BitsToFloat(0x3F800000, false) == 1.f);
    CHECK(YSE::DSP::BitsToFloat(-5, true) == -5.f);
    // Raw NaN and infinity patterns come back as 0.
    CHECK(YSE::DSP::BitsToFloat(0x7F800000, false) == 0.f);
    CHECK(YSE::DSP::BitsToFloat(static_cast<int>(0xFFC00000u), false) == 0.f);

    CHECK(YSE::DSP::BitModeFromInt(0) == BIT_MODE::RAW);
    CHECK(YSE::DSP::BitModeFromInt(3) == BIT_MODE::INTEGER_RAW);
    CHECK(YSE::DSP::BitModeFromInt(7) == BIT_MODE::RAW);
    CHECK(YSE::DSP::BitLeftIsInteger(BIT_MODE::INTEGER_RAW));
    CHECK_FALSE(YSE::DSP::BitLeftIsInteger(BIT_MODE::RAW_INTEGER));
    CHECK(YSE::DSP::BitRightIsInteger(BIT_MODE::RAW_INTEGER));
    CHECK_FALSE(YSE::DSP::BitRightIsInteger(BIT_MODE::INTEGER_RAW));
  }

  TEST_CASE("bitwise: raw and / or / xor with a mask match the bit pattern done by hand "
            "(#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    // Clear the low 16 mantissa bits: a coarse bitcrush that stays finite.
    const std::uint32_t crush = 0xFFFF0000u;
    YSE::DSP::bitwise andOp(YSE::DSP::bitwise::OP::AND, static_cast<int>(crush));
    CHECK(andOp.getOp() == YSE::DSP::bitwise::OP::AND);
    CHECK(andOp.getMask() == static_cast<int>(crush));
    CHECK(andOp.getMode() == BIT_MODE::RAW);
    YSE::DSP::buffer& out = andOp(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const float expected = FromBits(Bits(in.getPtr()[i]) & crush);
      CHECK(out.getPtr()[i] == expected);
      CHECK(std::fabs(out.getPtr()[i] - in.getPtr()[i]) <= std::fabs(in.getPtr()[i]) * 1e-2f);
    }

    // Or sets the lowest mantissa bit; xor with the sign bit negates.
    YSE::DSP::bitwise orOp(YSE::DSP::bitwise::OP::OR, 1);
    YSE::DSP::bitwise xorOp(YSE::DSP::bitwise::OP::XOR, static_cast<int>(0x80000000u));
    YSE::DSP::buffer& ored = orOp(in);
    for (unsigned int i = 0; i < ored.getLength(); ++i)
      CHECK(ored.getPtr()[i] == FromBits(Bits(in.getPtr()[i]) | 1u));
    YSE::DSP::buffer& negated = xorOp(in);
    for (unsigned int i = 0; i < negated.getLength(); ++i)
      CHECK(negated.getPtr()[i] == -in.getPtr()[i]);

    // setOp / setMask swap the operation on the same object.
    andOp.setOp(YSE::DSP::bitwise::OP::XOR).setMask(static_cast<int>(0x80000000u));
    YSE::DSP::buffer& again = andOp(in);
    for (unsigned int i = 0; i < again.getLength(); ++i)
      CHECK(again.getPtr()[i] == -in.getPtr()[i]);
  }

  TEST_CASE("bitwise: integer mode works on the converted integers (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::bitwise op(YSE::DSP::bitwise::OP::AND, 6, BIT_MODE::INTEGER);
    YSE::DSP::buffer& out = op(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const int n = static_cast<int>(in.getPtr()[i]);
      CHECK(out.getPtr()[i] == static_cast<float>(n & 6));
    }
    op.setOp(YSE::DSP::bitwise::OP::OR).setMask(1);
    YSE::DSP::buffer& ored = op(in);
    for (unsigned int i = 0; i < ored.getLength(); ++i) {
      const int n = static_cast<int>(in.getPtr()[i]);
      CHECK(ored.getPtr()[i] == static_cast<float>(n | 1));
    }
    op.setOp(YSE::DSP::bitwise::OP::XOR).setMask(-1);
    YSE::DSP::buffer& flipped = op(in);
    for (unsigned int i = 0; i < flipped.getLength(); ++i) {
      const int n = static_cast<int>(in.getPtr()[i]);
      CHECK(flipped.getPtr()[i] == static_cast<float>(~n));
    }
    // process() agrees with the block.
    CHECK(YSE::DSP::bitwise::process(YSE::DSP::bitwise::OP::AND, BIT_MODE::INTEGER, 7.9f, 5) ==
          5.f);
    CHECK(YSE::DSP::bitwise::process(YSE::DSP::bitwise::OP::AND, BIT_MODE::RAW, 3.f,
                                     static_cast<int>(0xFF800000u)) == 2.f);
  }

  TEST_CASE("bitwise: a second signal is read with the right half of the mode (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    for (unsigned int i = 0; i < right.getLength(); ++i)
      right.getPtr()[i] = static_cast<float>(i % 8);

    YSE::DSP::bitwise op(YSE::DSP::bitwise::OP::AND);
    for (int m = 0; m < 4; ++m) {
      const BIT_MODE mode = YSE::DSP::BitModeFromInt(m);
      CAPTURE(m);
      op.setMode(mode);
      YSE::DSP::buffer& out = op(in, right);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      const bool li = YSE::DSP::BitLeftIsInteger(mode);
      const bool ri = YSE::DSP::BitRightIsInteger(mode);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        const std::uint32_t a = li ? static_cast<std::uint32_t>(static_cast<int>(in.getPtr()[i]))
                                   : Bits(in.getPtr()[i]);
        const std::uint32_t b = ri ? static_cast<std::uint32_t>(static_cast<int>(right.getPtr()[i]))
                                   : Bits(right.getPtr()[i]);
        const std::uint32_t r = a & b;
        const float expected = li ? static_cast<float>(static_cast<int>(r))
                                  : (std::isfinite(FromBits(r)) ? FromBits(r) : 0.f);
        CHECK(out.getPtr()[i] == expected);
      }
    }

    // The block is as long as the shorter input.
    YSE::DSP::buffer shortRight(32);
    shortRight = 1.f;
    CHECK(op(in, shortRight).getLength() == 32);
  }

  TEST_CASE("bitwise: no NaN or infinity leaves a raw result (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    // Or-ing in every exponent bit makes every sample NaN or infinity.
    YSE::DSP::bitwise op(YSE::DSP::bitwise::OP::OR, 0x7F800000);
    YSE::DSP::buffer& out = op(in);
    for (unsigned int i = 0; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == 0.f);
    in.getPtr()[3] = kNaN;
    in.getPtr()[4] = kInf;
    op.setOp(YSE::DSP::bitwise::OP::AND).setMask(-1);
    YSE::DSP::buffer& passed = op(in);
    CHECK(AllFinite(passed));
    CHECK(passed.getPtr()[3] == 0.f);
    CHECK(passed.getPtr()[5] == in.getPtr()[5]);
  }

  TEST_CASE("bitnot: raw inverts the bit pattern, integer gives -n - 1 (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::bitnot raw;
    CHECK(raw.getMode() == BIT_MODE::RAW);
    YSE::DSP::buffer& out = raw(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const float f = FromBits(~Bits(in.getPtr()[i]));
      CHECK(out.getPtr()[i] == (std::isfinite(f) ? f : 0.f));
    }
    CHECK(YSE::DSP::bitnot::process(BIT_MODE::RAW, 1.f) == FromBits(0xC07FFFFFu)); // just under -4
    CHECK(YSE::DSP::bitnot::process(BIT_MODE::RAW, 0.f) == 0.f); // all ones is NaN: 0

    raw.setMode(BIT_MODE::INTEGER);
    YSE::DSP::buffer& ints = raw(in);
    for (unsigned int i = 0; i < ints.getLength(); ++i) {
      const int n = static_cast<int>(in.getPtr()[i]);
      CHECK(ints.getPtr()[i] == static_cast<float>(-n - 1));
    }
    // INTEGER_RAW reads the left word as an integer too.
    CHECK(YSE::DSP::bitnot::process(BIT_MODE::INTEGER_RAW, 5.5f) == -6.f);
  }

  TEST_CASE("bitshift: raw shifts the bit pattern, integer doubles and halves (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    for (int s : {-3, -1, 0, 1, 4}) {
      CAPTURE(s);
      YSE::DSP::bitshift raw(s);
      CHECK(raw.getShift() == s);
      YSE::DSP::buffer& out = raw(in);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        const std::uint32_t u = Bits(in.getPtr()[i]);
        const std::uint32_t r = s >= 0 ? u << s : u >> -s;
        const float f = FromBits(r);
        CHECK(out.getPtr()[i] == (std::isfinite(f) ? f : 0.f));
      }

      YSE::DSP::bitshift ints(s, BIT_MODE::INTEGER);
      YSE::DSP::buffer& iout = ints(in);
      for (unsigned int i = 0; i < iout.getLength(); ++i) {
        CAPTURE(i);
        const int n = static_cast<int>(in.getPtr()[i]);
        const double expected = s >= 0 ? n * std::pow(2.0, s) : std::floor(n / std::pow(2.0, -s));
        CHECK(iout.getPtr()[i] == static_cast<float>(expected));
      }
    }

    // Shifting a raw 1.0 right by 23 moves the exponent into the mantissa;
    // a raw right shift is logical, so the sign bit does not spread.
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::RAW, -23, 1.f) == FromBits(0x7Fu));
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::RAW, -1, -1.f) == FromBits(0xBF800000u >> 1));
    // 32 bits or more empty the word.
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::RAW, 32, 1.f) == 0.f);
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::INTEGER, 40, 3.f) == 0.f);
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::INTEGER, -40, -3.f) == -1.f);
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::INTEGER, -40, 3.f) == 0.f);
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::INTEGER, -2147483647 - 1, -3.f) == -1.f);
    // Integer left shift wraps at 32 bits.
    CHECK(YSE::DSP::bitshift::process(BIT_MODE::INTEGER, 31, 1.f) == -2147483648.f);

    YSE::DSP::bitshift s;
    s.setShift(2).setMode(BIT_MODE::INTEGER);
    CHECK(s.getMode() == BIT_MODE::INTEGER);
    YSE::DSP::buffer one(YSE::STANDARD_BUFFERSIZE);
    one = 3.f;
    CHECK(s(one).getPtr()[0] == 12.f);
  }

  TEST_CASE("bitsafe: NaN and infinity become 0, everything else passes (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    in.getPtr()[0] = kNaN;
    in.getPtr()[1] = kInf;
    in.getPtr()[2] = -kInf;
    in.getPtr()[3] = std::numeric_limits<float>::max();
    in.getPtr()[4] = std::numeric_limits<float>::denorm_min();
    in.getPtr()[5] = -0.f;
    YSE::DSP::bitsafe safe;
    YSE::DSP::buffer& out = safe(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(out.getPtr()[1] == 0.f);
    CHECK(out.getPtr()[2] == 0.f);
    for (unsigned int i = 3; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(Bits(out.getPtr()[i]) == Bits(in.getPtr()[i]));
    }
    CHECK(YSE::DSP::bitsafe::process(kNaN) == 0.f);
    CHECK(YSE::DSP::bitsafe::process(0.25f) == 0.25f);

    // In place, for C++ modules that own their buffer.
    YSE::DSP::bitsafe::apply(in);
    CHECK(AllFinite(in));
    CHECK(in.getPtr()[6] == out.getPtr()[6]);
  }

  TEST_CASE("bitwise family: processing allocates nothing (#1051)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    right = 3.f;
    YSE::DSP::bitwise op(YSE::DSP::bitwise::OP::XOR, 5);
    YSE::DSP::bitnot inv;
    YSE::DSP::bitshift sh(2);
    YSE::DSP::bitsafe safe;
    {
      TestHelpers::ProbeScope probe;
      for (int m = 0; m < 4; ++m) {
        op.setMode(YSE::DSP::BitModeFromInt(m));
        op(in);
        op(in, right);
        inv.setMode(YSE::DSP::BitModeFromInt(m));
        inv(in);
        sh.setMode(YSE::DSP::BitModeFromInt(m));
        sh(in);
      }
      safe(in);
      YSE::DSP::bitsafe::apply(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
