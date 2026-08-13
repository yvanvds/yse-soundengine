// Tests for YSE::DSP::buffer (YseEngine/dsp/buffer.hpp / buffer.cpp).
//
// NOTE — known bug in buffer::maxValue():
//   The SIMD unrolled loop (triggered for buffers with length >= 8) checks
//   ptr1[1..7] > max but then assigns ptr1[0] instead of the matching element.
//   Tests that exercise maxValue() therefore use buffers with length < 8 so
//   only the scalar tail loop runs. Track via a GitHub issue when fixing.
//
// NOTE — buffer::cursor is a public raw pointer the buffer itself never
//   advances, but every constructor does define it: the length constructor and
//   the copy constructor both park it at the start of their own storage, and so
//   does copy-assignment (issue #816), and resize() re-parks it on the storage
//   it just moved (issue #818). Tests may therefore read it; what they must not
//   assume is that it tracks anything, since only calling code moves it.

#include <doctest/doctest.h>

#include <cstring>
#include <new>
#include <vector>

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

  // Issue #814: copy assignment used to hand resize() the source's *storage*
  // size (length + tail) while resize() adds this buffer's tail on top, so the
  // destination came out one tail too long and the copy loop -- bounded by the
  // destination -- read past the end of the source allocation (ASan:
  // heap-buffer-overflow). The destination must end up exactly as long as the
  // source, whatever the two started out as.

  TEST_CASE("buffer: copy assignment from a shorter source shrinks the destination") {
    YSE::DSP::buffer src(4), dst(16);
    src = 2.0f;
    dst = -1.0f;
    dst = src;
    CHECK(dst.getLength() == 4u);
    for (unsigned i = 0; i < 4; ++i)
      CHECK(dst.getPtr()[i] == doctest::Approx(2.0f));
  }

  TEST_CASE("buffer: copy assignment from a longer source grows the destination") {
    YSE::DSP::buffer src(16), dst(4);
    src = 3.0f;
    dst = -1.0f;
    dst = src;
    CHECK(dst.getLength() == 16u);
    for (unsigned i = 0; i < 16; ++i)
      CHECK(dst.getPtr()[i] == doctest::Approx(3.0f));
  }

  TEST_CASE("buffer: copy assignment across lengths keeps the overflow tail intact") {
    // Buffers with an overflow tail (wavetables use one) are the case that
    // tripped #814: the tail was counted twice, so the destination grew past
    // the source and the last sample was read out of bounds.
    YSE::DSP::buffer src(4, 1), dst(16, 1);
    dst = -1.0f;
    for (unsigned i = 0; i < 4; ++i)
      src.getPtr()[i] = static_cast<float>(i) + 1.f;
    src.copyOverflow(); // tail sample mirrors sample 0

    dst = src;
    CHECK(dst.getLength() == 4u);
    for (unsigned i = 0; i < 4; ++i)
      CHECK(dst.getPtr()[i] == doctest::Approx(static_cast<float>(i) + 1.f));
    // The wrap-around tail must be the source's tail, not stale or out-of-bounds data.
    CHECK(dst.getPtr()[dst.getLength()] == doctest::Approx(dst.getPtr()[0]));
  }

  TEST_CASE("buffer: copy assignment adopts the source's overflow") {
    YSE::DSP::buffer src(8, 1), dst(8, 0);
    src = 1.0f;
    dst = src;
    CHECK(dst.getLength() == 8u);
    CHECK(dst.getPtr()[8] == doctest::Approx(1.0f)); // tail exists and was copied
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

  // Issue #818: resize() forwarded straight to the storage vector, so a resize
  // that grew past the current capacity reallocated and freed the block cursor
  // pointed into. A caller that parked a position before the resize was then
  // reading through a dangling pointer. resize() keeps the samples it does not
  // drop, so the cursor is re-parked at the same sample on the new storage.
  //
  // The dereferences below are deliberate: on unfixed code they read freed
  // memory, which is what makes the defect an ASan report and not just a
  // failing compare. They are safe once resize() re-parks the cursor.

  TEST_CASE("buffer: resize re-parks the cursor on the new storage") {
    YSE::DSP::buffer b(4);
    b = 5.0f;
    b.cursor = b.getPtr() + 2; // caller parked a read position

    b.resize(4096); // grows past capacity: the old block is freed

    CHECK(b.cursor == b.getPtr() + 2); // same sample, new allocation
    CHECK(*b.cursor == doctest::Approx(5.0f));
  }

  TEST_CASE("buffer: resize clamps a cursor past the new end") {
    YSE::DSP::buffer b(64);
    b = 1.0f;
    b.cursor = b.getPtr() + 40;

    b.resize(8); // the sample the cursor stood on is gone

    CHECK(b.cursor == b.getPtr() + b.getLength()); // clamped to the new end
    CHECK(b.getLength() == 8u);
  }

  TEST_CASE("buffer: resize keeps the cursor usable through repeated growth") {
    // The engine shape of the bug: a buffer whose length follows its input
    // (filters, oscillators, fft all resize per block) with a caller-owned read
    // head parked in it. Every growth here reallocates at least once.
    YSE::DSP::buffer b(8);
    b = 0.0f;
    b.cursor = b.getPtr() + 1;

    for (unsigned int len = 16; len <= 2048u; len *= 2) {
      b.resize(len, 0.25f);
      REQUIRE(b.cursor == b.getPtr() + 1);
      *b.cursor = static_cast<float>(len); // write through the re-parked cursor
      CHECK(b.getPtr()[1] == doctest::Approx(static_cast<float>(len)));
    }
  }

  TEST_CASE("buffer: resize leaves a foreign cursor at the start of our storage") {
    // A cursor that was never parked in this buffer cannot be rebased (and
    // differencing unrelated pointers is undefined), so resize() falls back to
    // the documented start-of-storage position rather than inventing an offset.
    YSE::DSP::buffer b(4), other(64);
    b.cursor = other.getPtr() + 10;

    b.resize(256);

    CHECK(b.cursor == b.getPtr());
  }

  TEST_CASE("buffer: cursor and sampleRateAdjustment are initialised after construction") {
    YSE::DSP::buffer b(8);
    CHECK(b.cursor == b.getPtr());
    CHECK(b.getSampleRateAdjustment() == doctest::Approx(1.0f));
  }

  // Issue #816: the copy constructor initialised only `storage`, so the copy's
  // sampleRateAdjustment and cursor kept whatever bytes its memory happened to
  // hold, and copy-assignment copied samples only, silently leaving the
  // destination's own (now wrong) sampleRateAdjustment in place. Both make a
  // copied buffer describe audio it does not contain: the rate ratio is what
  // tells a consumer how fast to read the samples.

  TEST_CASE("buffer: copy assignment adopts the source's sample rate adjustment") {
    YSE::DSP::buffer src(8), dst(8);
    src = 0.25f;
    src.setSampleRateAdjustment(0.5f); // e.g. a 24 kHz source at a 48 kHz engine rate
    dst.setSampleRateAdjustment(2.0f); // destination was holding a 96 kHz source

    dst = src;

    // Without the fix this stayed at 2.0f: the copy holds src's samples but
    // would be played back at four times the speed they were captured at.
    CHECK(dst.getSampleRateAdjustment() == doctest::Approx(0.5f));
  }

  TEST_CASE("buffer: copy assignment re-parks cursor in the destination's own storage") {
    YSE::DSP::buffer src(64), dst(4);
    src = 1.0f;
    dst.cursor = dst.getPtr() + 3; // caller parked a read position

    dst = src; // grows 4 -> 64: the old storage block is gone

    // The pre-assignment cursor points into a freed allocation, so leaving it
    // alone is not an option; it is re-parked at the start of the new storage.
    CHECK(dst.cursor == dst.getPtr());
  }

  TEST_CASE("buffer: copy construction over dirty memory still yields a defined buffer") {
    // An indeterminate member usually reads back as "whatever was there", which
    // is not something a test can assert on. Placement-new the copy over a byte
    // pattern of our own choosing so the defect becomes observable: before the
    // fix the copy reported the poison value as its sample-rate adjustment and
    // a poison pointer as its cursor.
    YSE::DSP::buffer src(8);
    src = 0.25f;
    src.setSampleRateAdjustment(0.5f);

    alignas(YSE::DSP::buffer) unsigned char raw[sizeof(YSE::DSP::buffer)];
    std::memset(raw, 0x5A, sizeof(raw)); // 0x5A5A5A5A reads as ~1.5e16f

    YSE::DSP::buffer* copy = new (static_cast<void*>(raw)) YSE::DSP::buffer(src);

    CHECK(copy->getSampleRateAdjustment() == doctest::Approx(0.5f));
    CHECK(copy->cursor == copy->getPtr());
    CHECK(copy->getLength() == 8u);
    CHECK(copy->getPtr()[7] == doctest::Approx(0.25f));

    copy->~buffer();
  }

  TEST_CASE("buffer: a vector reallocation preserves the sample rate adjustment") {
    // buffer declares a copy constructor, which suppresses the implicit move
    // constructor, so vector growth copy-constructs every element. This is the
    // reachable shape of the bug: buffers kept in a std::vector (sampler
    // channels, wavetable banks) are silently copied when the vector grows.
    std::vector<YSE::DSP::buffer> bank;
    bank.reserve(1);
    bank.emplace_back(8u);
    bank[0] = 0.25f;
    bank[0].setSampleRateAdjustment(0.75f);

    bank.emplace_back(8u); // reallocates: element 0 is copy-constructed

    CHECK(bank[0].getSampleRateAdjustment() == doctest::Approx(0.75f));
    CHECK(bank[0].cursor == bank[0].getPtr());
    CHECK(bank[0].getPtr()[0] == doctest::Approx(0.25f));
  }

  TEST_CASE("buffer: maxValue returns correct result when maximum is not at index 0") {
    YSE::DSP::buffer b(8);
    b = 0.5f;
    b.getPtr()[5] = 1.0f; // max at index 5, not 0
    CHECK(b.maxValue() == doctest::Approx(1.0f).epsilon(1e-5f));
  }

} // TEST_SUITE("dsp")
