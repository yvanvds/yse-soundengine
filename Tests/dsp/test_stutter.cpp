// Tests for YSE::DSP::stutter (YseEngine/dsp/sampling/stutter.hpp), issue
// #1150: a capture buffer whose last fragment is replayed by phase, what
// ~stutter does. The fragment a trigger copies against the input it
// recorded, sample-accurate edges of either polarity and the bang,
// playback at a ramp as the input back again, interpolation and phase
// wrapping, several outputs, size and maxSize limits, a copy spread over
// blocks, clear, the chances (repeat, dropout, ampvar) with a seed,
// copyTo into a buffer, the sample rate, and allocation-free blocks.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/stutter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferWriter;
using YSE::DSP::sampleWriteView;
using YSE::DSP::stutter;

namespace {

  // Not `signal`: bionic's <signal.h> puts ::signal in the global namespace.
  using Input = stutter::signal;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  // Input sample n (counted from the first block) is n + 1, so a fragment
  // names the samples it holds.
  struct Counter {
    UInt next = 0;
    buffer block{STANDARD_BUFFERSIZE};
    buffer& Next(UInt length = STANDARD_BUFFERSIZE) {
      block.resize(length);
      for (UInt i = 0; i < length; ++i)
        block.getPtr()[i] = static_cast<float>(++next);
      return block;
    }
  };

  // A block that is 0 except for 1 at sample `at` (none when past the end).
  buffer Pulse(UInt at, UInt length = STANDARD_BUFFERSIZE) {
    buffer b(length);
    b = 0.f;
    if (at < length) b.getPtr()[at] = 1.f;
    return b;
  }

  // One block with one phase signal for every output.
  void Block(stutter& st, buffer& in, Input trigger, Input phase) {
    st(in, trigger, phase);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("stutter: a trigger copies the samples that end at its edge (#1150)") {
    stutter st(1000);
    st.size(10);
    Counter c;
    // Block 0: no trigger, silence.
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.length() == 0);
    CHECK(st.out(0).getPtr()[STANDARD_BUFFERSIZE - 1] == 0.f);
    // Block 1: an edge at sample 100, i.e. input sample 512 + 100 + 1 = 613.
    buffer trig = Pulse(100);
    Block(st, c.Next(), Input(trig), 0.f);
    REQUIRE(st.length() == 10);
    for (UInt k = 0; k < 10; ++k) {
      CAPTURE(k);
      CHECK(st.fragment(k) == static_cast<float>(STANDARD_BUFFERSIZE + 100 + 1 - 9 + k));
    }
    // Phase 0 reads the first fragment sample, from the edge on; silence
    // before it.
    CHECK(st.out(0).getPtr()[99] == 0.f);
    CHECK(st.out(0).getPtr()[100] == st.fragment(0));
    CHECK(st.out(0).getPtr()[STANDARD_BUFFERSIZE - 1] == st.fragment(0));
    // A trigger held at 0 copies nothing new.
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.fragment(0) == static_cast<float>(STANDARD_BUFFERSIZE + 92));
  }

  TEST_CASE("stutter: a ramp phase plays the fragment back as it was recorded (#1150)") {
    // The closed form: phase n / L at sample n reads fragment sample n, so
    // a ramp across L samples gives the recorded input back, in a loop.
    constexpr UInt L = 64;
    stutter st(4096);
    st.size(L);
    Counter c;
    buffer trig = Pulse(STANDARD_BUFFERSIZE - 1);
    Block(st, c.Next(), Input(trig), 0.f); // copies inputs 449 .. 512
    REQUIRE(st.length() == L);
    buffer ramp(STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
      ramp.getPtr()[i] = static_cast<float>(i % L) / static_cast<float>(L);
    Block(st, c.Next(), 0.f, Input(ramp));
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(st.out(0).getPtr()[i] == static_cast<float>(STANDARD_BUFFERSIZE - L + 1 + i % L));
    }
  }

  TEST_CASE("stutter: reads between samples on a line, wrapping round the fragment (#1150)") {
    stutter st(100);
    st.size(4);
    buffer in(4);
    const float values[4] = {1.f, 3.f, 5.f, 7.f};
    for (UInt i = 0; i < 4; ++i)
      in.getPtr()[i] = values[i];
    buffer trig = Pulse(3, 4);
    Block(st, in, Input(trig), 0.f);
    REQUIRE(st.length() == 4);
    auto at = [&](float phase) {
      buffer silent(1);
      silent = 0.f;
      st(silent, 0.f, phase);
      return st.out(0).getPtr()[0];
    };
    CHECK(at(0.f) == 1.f);
    CHECK(at(0.125f) == doctest::Approx(2.f)); // halfway from sample 0 to 1
    CHECK(at(0.5f) == 5.f);
    CHECK(at(0.875f) == doctest::Approx(4.f)); // halfway from 7 back to 1
    CHECK(at(1.f) == 1.f); // wraps to 0
    CHECK(at(1.25f) == 3.f);
    CHECK(at(-0.25f) == 7.f);
    CHECK(at(std::numeric_limits<float>::quiet_NaN()) == 0.f);
    CHECK(at(std::numeric_limits<float>::infinity()) == 0.f);
  }

  TEST_CASE("stutter: polarity, a trigger held high, and the bang (#1150)") {
    stutter st(1000);
    st.size(1);
    Counter c;
    // Positive: a constant 1 triggers once, at its first sample.
    Block(st, c.Next(), 1.f, 0.f);
    CHECK(st.fragment(0) == 1.f);
    Block(st, c.Next(), 1.f, 0.f);
    CHECK(st.fragment(0) == 1.f);
    // Negative: a fall below 0. Going up again does nothing.
    st.polarity(false);
    CHECK_FALSE(st.polarity());
    buffer down(STANDARD_BUFFERSIZE);
    down = 1.f;
    down.getPtr()[10] = -0.5f;
    Block(st, c.Next(), Input(down), 0.f);
    CHECK(st.fragment(0) == static_cast<float>(2 * STANDARD_BUFFERSIZE + 11));
    buffer up = Pulse(20);
    Block(st, c.Next(), Input(up), 0.f);
    CHECK(st.fragment(0) == static_cast<float>(2 * STANDARD_BUFFERSIZE + 11));
    // The bang: at the first sample of the next block.
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.fragment(0) == static_cast<float>(4 * STANDARD_BUFFERSIZE + 1));
    // An empty block keeps it for the next one with samples.
    st.trigger();
    Block(st, c.Next(0), 0.f, 0.f);
    CHECK(st.fragment(0) == static_cast<float>(4 * STANDARD_BUFFERSIZE + 1));
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.fragment(0) == static_cast<float>(5 * STANDARD_BUFFERSIZE + 1));
  }

  TEST_CASE("stutter: several outputs read at their own phases (#1150)") {
    stutter st(100, 3);
    REQUIRE(st.outputs() == 3);
    st.size(4);
    buffer in(4);
    for (UInt i = 0; i < 4; ++i)
      in.getPtr()[i] = static_cast<float>(10 * (i + 1));
    buffer trig = Pulse(3, 4);
    const Input phases[3] = {0.f, 0.25f, 0.75f};
    st(4, Input(in), Input(trig), phases);
    CHECK(st.out(0).getPtr()[3] == 10.f);
    CHECK(st.out(1).getPtr()[3] == 20.f);
    CHECK(st.out(2).getPtr()[3] == 40.f);
    // The limits.
    CHECK(stutter(10, 0).outputs() == 1);
    CHECK(stutter(10, 99).outputs() == stutter::MAX_OUTPUTS);
    CHECK(stutter(0).capacity() == 1);
  }

  TEST_CASE("stutter: size and maxSize, and a fragment across the record buffer's end "
            "(#1150)") {
    stutter st(100);
    CHECK(st.capacity() == 100);
    CHECK(st.maxSize() == 100);
    CHECK(st.size() == 100); // DEFAULT_SIZE held to the capacity
    CHECK(stutter().size() == stutter::DEFAULT_SIZE);
    CHECK(stutter().capacity() == stutter::DEFAULT_CAPACITY);
    st.size(500);
    CHECK(st.size() == 100);
    st.maxSize(30);
    CHECK(st.size() == 30);
    st.size(40);
    CHECK(st.size() == 30);
    st.maxSize(1000);
    CHECK(st.maxSize() == 100);

    // 150 samples into a ring of 100, then a 30-sample fragment that spans
    // its end: inputs 121 .. 150.
    st.size(30);
    Counter c;
    Block(st, c.Next(100), 0.f, 0.f);
    Block(st, c.Next(50), 0.f, 0.f);
    st.trigger();
    Block(st, c.Next(0), 0.f, 0.f);
    // The bang copies at the next sample recorded: inputs 122 .. 151.
    Block(st, c.Next(1), 0.f, 0.f);
    REQUIRE(st.length() == 30);
    for (UInt k = 0; k < 30; ++k)
      CHECK(st.fragment(k) == static_cast<float>(122 + k));

    // Size 0: an empty fragment, silence.
    st.size(0);
    st.trigger();
    Block(st, c.Next(), 0.f, 0.5f);
    CHECK(st.length() == 0);
    CHECK(st.out(0).getPtr()[0] == 0.f);
  }

  TEST_CASE("stutter: a copy rate spreads the copy over blocks (#1150)") {
    stutter st(10000);
    st.size(1000).copyRate(300);
    Counter c;
    while (c.next < 1500)
      Block(st, c.Next(), 0.f, 0.f);
    // The bang copies at the next block's first input: the 1000 inputs
    // that end with it.
    const float first = static_cast<float>(c.next + 1 - 999);
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f); // 300 copied
    REQUIRE(st.length() == 1000);
    CHECK(st.copying());
    CHECK(st.fragment(0) == first);
    CHECK(st.fragment(299) == first + 299.f);
    CHECK(st.fragment(300) == 0.f); // not yet: what was there before
    Block(st, c.Next(), 0.f, 0.f); // 600
    CHECK(st.fragment(599) == first + 599.f);
    CHECK(st.fragment(600) == 0.f);
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.copying());
    Block(st, c.Next(), 0.f, 0.f); // all 1000
    CHECK_FALSE(st.copying());
    for (UInt k = 0; k < 1000; ++k)
      CHECK(st.fragment(k) == first + static_cast<float>(k));
  }

  TEST_CASE("stutter: clear empties the recording and the fragment (#1150)") {
    stutter st(100);
    st.size(10);
    Counter c;
    st.trigger();
    Block(st, c.Next(50), 0.f, 0.f);
    REQUIRE(st.length() == 10);
    st.clear();
    CHECK(st.length() == 0);
    CHECK(st.fragment(0) == 0.f);
    // What was recorded before is gone: a bang at the next sample copies
    // nine zeros and that sample.
    st.trigger();
    buffer one(1);
    one = 5.f;
    Block(st, one, 0.f, 0.f);
    REQUIRE(st.length() == 10);
    for (UInt k = 0; k < 9; ++k)
      CHECK(st.fragment(k) == 0.f);
    CHECK(st.fragment(9) == 5.f);
  }

  TEST_CASE("stutter: repeat, dropout and ampvar chances, repeated by a seed (#1150)") {
    stutter st(1000);
    st.size(1);
    Counter c;
    CHECK(st.gain() == 1.f);
    CHECK_FALSE(st.droppedOut());
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.fragment(0) == 1.f);
    CHECK(st.out(0).getPtr()[5] == 1.f);

    // repeat 100: the old fragment plays on.
    st.repeatChance(100.f);
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.fragment(0) == 1.f);
    st.repeatChance(0.f);

    // dropout 100: silence until a trigger that does not drop out.
    st.dropoutChance(100.f);
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f);
    CHECK(st.droppedOut());
    CHECK(st.out(0).getPtr()[5] == 0.f);
    st.dropoutChance(0.f);
    st.trigger();
    Block(st, c.Next(), 0.f, 0.f);
    CHECK_FALSE(st.droppedOut());
    CHECK(st.out(0).getPtr()[5] == static_cast<float>(3 * STANDARD_BUFFERSIZE + 1));

    // Held to their ranges.
    CHECK(stutter().repeatChance(150.f).repeatChance() == 100.f);
    CHECK(stutter().dropoutChance(-3.f).dropoutChance() == 0.f);
    CHECK(stutter().ampVariation(2.f).ampVariation() == 1.f);
    CHECK(stutter().ampVariation(std::numeric_limits<float>::quiet_NaN()).ampVariation() == 0.f);

    // ampvar 1: gains spread over 0 .. 1; the same seed draws the same.
    stutter a(100);
    stutter b(100);
    a.size(1).ampVariation(1.f).seed(7);
    b.size(1).ampVariation(1.f).seed(7);
    buffer ones(4);
    ones = 1.f;
    float lowest = 1.f;
    float highest = 0.f;
    for (int n = 0; n < 200; ++n) {
      a.trigger();
      b.trigger();
      a(ones, 0.f, 0.f);
      b(ones, 0.f, 0.f);
      CHECK(a.gain() == b.gain());
      CHECK(a.gain() >= 0.f);
      CHECK(a.gain() <= 1.f);
      CHECK(a.out(0).getPtr()[3] == a.gain());
      lowest = std::min(lowest, a.gain());
      highest = std::max(highest, a.gain());
    }
    CHECK(lowest < 0.1f);
    CHECK(highest > 0.9f);

    // Chances at 50%: about half of the triggers each.
    stutter h(100);
    h.size(1).repeatChance(50.f).dropoutChance(50.f);
    int repeats = 0;
    int drops = 0;
    for (int n = 0; n < 1000; ++n) {
      buffer v(1);
      v = static_cast<float>(n + 1);
      const float before = h.fragment(0);
      h.trigger();
      h(v, 0.f, 0.f);
      if (h.fragment(0) == before) repeats++;
      if (h.droppedOut()) drops++;
    }
    CHECK(repeats > 400);
    CHECK(repeats < 600);
    CHECK(drops > 400);
    CHECK(drops < 600);
  }

  TEST_CASE("stutter: copyTo writes the fragment into a buffer (#1150)") {
    stutter st(100);
    st.size(5);
    Counter c;
    buffer trig = Pulse(9, 10);
    st(c.Next(10), Input(trig), 0.f); // inputs 6 .. 10
    auto b = sampleBuffer::create(2, 8);
    sampleBufferWriter writer(b);
    {
      sampleWriteView view = writer.pin();
      CHECK(st.copyTo(view, 1, 4) == 4); // cut at the buffer's end
      CHECK(st.copyTo(view, 2, 0) == 0); // no such channel
    }
    std::vector<float> got(8);
    b->getSamples(1, 0, got.data(), 8);
    CHECK(got == std::vector<float>{0.f, 0.f, 0.f, 0.f, 6.f, 7.f, 8.f, 9.f});
    stutter empty(10);
    sampleWriteView view = writer.pin();
    CHECK(empty.copyTo(view, 0, 0) == 0);
  }

  TEST_CASE("stutter: no state follows the sample rate (#1150)") {
    SampleRateGuard guard;
    for (UInt rate : {22050u, 96000u}) {
      YSE::SAMPLERATE = rate;
      stutter st(1000);
      st.size(8);
      Counter c;
      buffer trig = Pulse(20);
      buffer& in = c.Next();
      st(in, Input(trig), 0.5f);
      CHECK(st.length() == 8);
      CHECK(st.out(0).getPtr()[30] == 18.f); // inputs 14 .. 21, sample 4
    }
  }

  TEST_CASE("stutter: processing allocates nothing (#1150)") {
    stutter st(48000, 4);
    st.size(4000).copyRate(256).ampVariation(0.5f).dropoutChance(20.f).repeatChance(20.f);
    buffer in(STANDARD_BUFFERSIZE);
    buffer trig(STANDARD_BUFFERSIZE);
    buffer ramp(STANDARD_BUFFERSIZE);
    buffer shorter(16);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f);
      trig.getPtr()[i] = (i % 100) < 50 ? 1.f : 0.f;
      ramp.getPtr()[i] = static_cast<float>(i) / static_cast<float>(STANDARD_BUFFERSIZE);
    }
    shorter = 0.5f;
    const Input phases[4] = {Input(ramp), 0.25f, Input(ramp), 0.75f};
    auto b = sampleBuffer::create(1, 8000);
    sampleBufferWriter writer(b);
    float last = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 20; ++block) {
        st.size(static_cast<UInt>(1000 * (block % 5)));
        st(STANDARD_BUFFERSIZE, Input(in), Input(trig), phases);
        st(shorter, 0.f, 0.f); // down and back up, within capacity
        st.trigger();
        if (block % 7 == 0) st.clear();
        sampleWriteView view = writer.pin();
        st.copyTo(view, 0, 0);
        last += st.out(0).getPtr()[0];
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(std::isfinite(last));
  }

} // TEST_SUITE
