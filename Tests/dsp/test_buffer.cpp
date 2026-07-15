// Tests for YSE::DSP::buffer (YseEngine/dsp/buffer.hpp / buffer.cpp).
//
// NOTE — known bug in buffer::maxValue():
//   The SIMD unrolled loop (triggered for buffers with length >= 8) checks
//   ptr1[1..7] > max but then assigns ptr1[0] instead of the matching element.
//   Tests that exercise maxValue() therefore use buffers with length < 8 so
//   only the scalar tail loop runs. Track via a GitHub issue when fixing.
//
// NOTE — buffer::cursor is a public raw pointer that is NOT initialised by any
//   constructor.  Do not read or write cursor in tests; it holds garbage until
//   the caller sets it explicitly.

#include <doctest/doctest.h>
#include "dsp/buffer.hpp"
#include "headers/constants.hpp" // YSE::STANDARD_BUFFERSIZE

TEST_SUITE("dsp") {

  TEST_CASE("buffer: default construction uses STANDARD_BUFFERSIZE") {
    YSE::DSP::buffer b;
    CHECK(b.getLength() == YSE::STANDARD_BUFFERSIZE); // 128
  }

  TEST_CASE("buffer: explicit length") {
    YSE::DSP::buffer b(64);
    CHECK(b.getLength() == 64u);
  }

  TEST_CASE("buffer: freshly constructed buffer is silent (all zeros)") {
    YSE::DSP::buffer b(8);
    CHECK(b.isSilent());
  }

  TEST_CASE("buffer: scalar assignment fills every element") {
    YSE::DSP::buffer b(8);
    b = 2.5f;
    CHECK(!b.isSilent());
    const float* ptr = b.getPtr();
    CHECK(ptr[0] == doctest::Approx(2.5f));
    CHECK(ptr[7] == doctest::Approx(2.5f));
  }

  TEST_CASE("buffer: scalar arithmetic operators") {
    YSE::DSP::buffer b(5); // length 5 stays in the scalar loop for all ops
    b = 4.0f;

    b += 1.0f;
    CHECK(b.getPtr()[0] == doctest::Approx(5.0f));
    CHECK(b.getPtr()[4] == doctest::Approx(5.0f));

    b -= 2.0f;
    CHECK(b.getPtr()[0] == doctest::Approx(3.0f));

    b *= 2.0f;
    CHECK(b.getPtr()[0] == doctest::Approx(6.0f));

    b /= 3.0f;
    CHECK(b.getPtr()[0] == doctest::Approx(2.0f).epsilon(1e-5f));
    CHECK(b.getPtr()[4] == doctest::Approx(2.0f).epsilon(1e-5f));
  }

  TEST_CASE("buffer: divide by zero clamps to zero") {
    YSE::DSP::buffer b(5);
    b = 5.0f;
    b /= 0.0f;
    CHECK(b.getPtr()[0] == 0.0f);
    CHECK(b.getPtr()[4] == 0.0f);
  }

  TEST_CASE("buffer: buffer-to-buffer add") {
    YSE::DSP::buffer a(5), bx(5);
    a = 3.0f;
    bx = 2.0f;
    a += bx;
    CHECK(a.getPtr()[0] == doctest::Approx(5.0f));
    CHECK(a.getPtr()[4] == doctest::Approx(5.0f));
  }

  TEST_CASE("buffer: buffer-to-buffer subtract") {
    YSE::DSP::buffer a(5), bx(5);
    a = 7.0f;
    bx = 3.0f;
    a -= bx;
    CHECK(a.getPtr()[0] == doctest::Approx(4.0f));
  }

  TEST_CASE("buffer: buffer-to-buffer multiply") {
    YSE::DSP::buffer a(5), bx(5);
    a = 3.0f;
    bx = 4.0f;
    a *= bx;
    CHECK(a.getPtr()[0] == doctest::Approx(12.0f));
  }

  TEST_CASE("buffer: buffer-to-buffer divide by zero clamps to zero") {
    YSE::DSP::buffer a(5), bx(5);
    a = 5.0f;
    bx = 0.0f;
    a /= bx;
    CHECK(a.getPtr()[0] == 0.0f);
  }

  TEST_CASE("buffer: isSilent detects non-zero value") {
    YSE::DSP::buffer b(5);
    CHECK(b.isSilent());
    b.getPtr()[2] = 0.001f;
    CHECK(!b.isSilent());
    b = 0.0f;
    CHECK(b.isSilent());
  }

  TEST_CASE("buffer: maxValue with length < 8 (scalar path only)") {
    YSE::DSP::buffer b(5);
    b = 0.1f;
    b.getPtr()[2] = 0.7f;
    CHECK(b.maxValue() == doctest::Approx(0.7f).epsilon(1e-5f));
  }

  TEST_CASE("buffer: maxValue returns initial sentinel -100 for all-negative buffer") {
    // maxValue() starts at -100.f; a buffer of all -200.f should return -100.f
    // because the scalar comparison `*ptr1 > max` is never true.
    YSE::DSP::buffer b(5);
    b = -200.0f;
    CHECK(b.maxValue() == doctest::Approx(-100.0f));
  }

  TEST_CASE("buffer: copy constructor") {
    YSE::DSP::buffer a(5);
    a = 3.14f;
    YSE::DSP::buffer b(a);
    CHECK(b.getLength() == 5u);
    CHECK(b.getPtr()[0] == doctest::Approx(3.14f).epsilon(1e-5f));
    CHECK(b.getPtr()[4] == doctest::Approx(3.14f).epsilon(1e-5f));
  }

  TEST_CASE("buffer: copy assignment") {
    YSE::DSP::buffer a(5), b(5);
    a = 1.5f;
    b = a;
    CHECK(b.getPtr()[0] == doctest::Approx(1.5f));
    // Modifying b must not affect a
    b = 9.0f;
    CHECK(a.getPtr()[0] == doctest::Approx(1.5f));
  }

  TEST_CASE("buffer: swap") {
    YSE::DSP::buffer a(5), b(5);
    a = 1.0f;
    b = 2.0f;
    a.swap(b);
    CHECK(a.getPtr()[0] == doctest::Approx(2.0f));
    CHECK(b.getPtr()[0] == doctest::Approx(1.0f));
  }

  TEST_CASE("buffer: copyFrom") {
    YSE::DSP::buffer src(5), dst(5);
    src = 0.0f;
    src.getPtr()[3] = 9.9f;
    dst = 1.0f;
    // Copy 1 element from position 3 of src into position 0 of dst
    dst.copyFrom(src, 3, 0, 1);
    CHECK(dst.getPtr()[0] == doctest::Approx(9.9f).epsilon(1e-5f));
    CHECK(dst.getPtr()[1] == doctest::Approx(1.0f)); // unchanged
  }

  TEST_CASE("buffer: resize grows and initialises new elements") {
    YSE::DSP::buffer b(4);
    b = 5.0f;
    b.resize(6, 0.0f);
    CHECK(b.getLength() == 6u);
    CHECK(b.getPtr()[0] == doctest::Approx(5.0f)); // original preserved
    CHECK(b.getPtr()[5] == doctest::Approx(0.0f)); // new element initialised
  }

  TEST_CASE("buffer: cursor and sampleRateAdjustment are initialised after construction") {
    YSE::DSP::buffer b(8);
    CHECK(b.cursor == b.getPtr());
    CHECK(b.getSampleRateAdjustment() == doctest::Approx(1.0f));
  }

  TEST_CASE("buffer: maxValue returns correct result when maximum is not at index 0") {
    YSE::DSP::buffer b(8);
    b = 0.5f;
    b.getPtr()[5] = 1.0f; // max at index 5, not 0
    CHECK(b.maxValue() == doctest::Approx(1.0f).epsilon(1e-5f));
  }

} // TEST_SUITE("dsp")
