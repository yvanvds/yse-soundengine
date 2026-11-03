// Tests for YSE::DSP::tableLookup (YseEngine/dsp/sampling/tableLookup.hpp),
// issue #1148: a sample buffer read as a table of values at signal rate,
// what ~table does. Interpolation, the out-of-range rules, the input map and
// output scale, the position messages, the stepping and random input modes,
// closed-form reads, the sample rate, and allocation-free blocks. The view
// is a sampleBufferReader's, tested in test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/sampleAccess.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/tableLookup.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleIndexReader;
using YSE::DSP::sampleView;
using YSE::DSP::tableLookup;
using interp = YSE::DSP::tableLookup::interpolation;
using edge = YSE::DSP::tableLookup::outOfRange;
using mode = YSE::DSP::tableLookup::inputMode;
using trig = YSE::DSP::tableLookup::triggerMode;

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  std::shared_ptr<sampleBuffer> Table(std::initializer_list<float> values, UInt channels = 1) {
    auto b = sampleBuffer::create(channels, static_cast<UInt>(values.size()));
    std::vector<float> v(values);
    b->setSamples(0, 0, v.data(), static_cast<UInt>(v.size()));
    return b;
  }

  // Entry n is 10 * n.
  std::shared_ptr<sampleBuffer> Tens(UInt frames) {
    auto b = sampleBuffer::create(1, frames);
    std::vector<float> v(frames);
    for (UInt n = 0; n < frames; ++n)
      v[n] = 10.f * static_cast<float>(n);
    b->setSamples(0, 0, v.data(), frames);
    return b;
  }

  buffer Signal(std::initializer_list<float> values) {
    buffer out(static_cast<UInt>(values.size()));
    UInt i = 0;
    for (float v : values)
      out.getPtr()[i++] = v;
    return out;
  }

  // What the table reads after one `next`, as the block's first sample.
  float AfterNext(tableLookup& t, const sampleView& view) {
    t.next(view);
    return t(view, 1).getPtr()[0];
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("tableLookup: defaults, and the three interpolations (#1148)") {
    tableLookup t;
    CHECK(t.channel() == 0);
    CHECK(t.interp() == interp::NONE);
    CHECK(t.extend() == edge::ZERO);
    CHECK(t.mode() == mode::LOOKUP);
    CHECK(t.trigger() == trig::RISING);
    CHECK_FALSE(t.hasInputMap());
    CHECK_FALSE(t.hasOutputScale());
    CHECK(t.range() == 128);
    CHECK_FALSE(t.signedRange());

    auto b = Table({0.f, 5.f, 10.f, 20.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    // None: the entry at or below.
    CHECK(t.read(view, 1.f) == 5.f);
    CHECK(t.read(view, 1.5f) == 5.f);
    CHECK(t.read(view, 2.99f) == 10.f);
    // Linear: Max's own example, 1.5 between 5 and 10 is 7.5.
    t.interp(interp::LINEAR);
    CHECK(t.read(view, 1.5f) == 7.5f);
    CHECK(t.read(view, 2.25f) == doctest::Approx(12.5f));
    CHECK(t.read(view, 3.f) == 20.f); // the last entry exactly
    // Round: the nearest.
    t.interp(interp::ROUND);
    CHECK(t.read(view, 1.49f) == 5.f);
    CHECK(t.read(view, 1.5f) == 10.f);
    CHECK(t.read(view, 2.6f) == 20.f);

    // A block of indexes reads the same.
    t.interp(interp::LINEAR);
    buffer indexes = Signal({0.5f, 1.5f, 2.5f});
    buffer& out = t(view, indexes);
    REQUIRE(out.getLength() == 3);
    CHECK(out.getPtr()[0] == 2.5f);
    CHECK(out.getPtr()[1] == 7.5f);
    CHECK(out.getPtr()[2] == 15.f);
  }

  TEST_CASE("tableLookup: outside the table, by the extend rule (#1148)") {
    auto b = Tens(128);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // Zero.
    CHECK(t.read(view, -1.f) == 0.f);
    CHECK(t.read(view, -0.5f) == 0.f); // the entry below is -1
    CHECK(t.read(view, 128.f) == 0.f);
    CHECK(t.read(view, nan) == 0.f);
    CHECK(t.read(view, inf) == 0.f);
    // Extend: the first or the last.
    t.extend(edge::EXTEND);
    CHECK(t.read(view, -7.f) == 0.f);
    CHECK(t.read(view, 500.f) == 1270.f);
    CHECK(t.read(view, inf) == 1270.f);
    CHECK(t.read(view, -inf) == 0.f);
    CHECK(t.read(view, nan) == 0.f);
    // Wrap: Max's example, 130 in a table of 128 reads entry 2.
    t.extend(edge::WRAP);
    CHECK(t.read(view, 130.f) == 20.f);
    CHECK(t.read(view, -1.f) == 1270.f);
    CHECK(t.read(view, -129.f) == 1270.f);
    CHECK(t.read(view, 256.f + 5.f) == 50.f);
    CHECK(t.read(view, inf) == 0.f);
    // Hold: the output stays where it was.
    t.extend(edge::HOLD);
    CHECK(t.read(view, 4.f) == 40.f);
    CHECK(t.read(view, 200.f) == 40.f);
    CHECK(t.read(view, -3.f) == 40.f);
    CHECK(t.read(view, nan) == 40.f);
    CHECK(t.read(view, 9.f) == 90.f);
    CHECK(t.read(view, 1000.f) == 90.f);

    // Linear interpolation past the last entry follows the same rule.
    t.interp(interp::LINEAR);
    t.extend(edge::ZERO);
    CHECK(t.read(view, 127.5f) == 635.f); // halfway from 1270 to 0
    t.extend(edge::EXTEND);
    CHECK(t.read(view, 127.5f) == 1270.f);
    t.extend(edge::WRAP);
    CHECK(t.read(view, 127.5f) == 635.f); // halfway from 1270 to entry 0
    CHECK(t.read(view, -0.5f) == 635.f);
    t.extend(edge::HOLD);
    CHECK(t.read(view, 10.f) == 100.f);
    CHECK(t.read(view, 127.5f) == 100.f); // needs an entry outside: held
    CHECK(t.read(view, 127.f) == 1270.f); // exactly the last needs only it
  }

  TEST_CASE("tableLookup: no interpolation and extend read what sampleIndexReader does "
            "(#1148)") {
    // The shared rule of the sampling family: truncated and clamped.
    auto b = Tens(16);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.extend(edge::EXTEND);
    sampleIndexReader index;
    buffer indexes = Signal({-3.f, -0.5f, 0.f, 0.9f, 7.5f, 15.f, 15.99f, 16.f, 1e9f,
                             std::numeric_limits<float>::quiet_NaN()});
    buffer& mine = t(view, indexes);
    buffer& theirs = index(view, indexes);
    for (UInt i = 0; i < indexes.getLength(); ++i) {
      CAPTURE(i);
      CHECK(mine.getPtr()[i] == theirs.getPtr()[i]);
    }
  }

  TEST_CASE("tableLookup: a ramp table, linearly interpolated, in closed form (#1148)") {
    // Entry n is 10 n, so a linear read at x is 10 x for 0 <= x <= 127.
    auto b = Tens(128);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.interp(interp::LINEAR);
    buffer indexes(YSE::STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
      indexes.getPtr()[i] = 127.f * static_cast<float>(i) / (YSE::STANDARD_BUFFERSIZE - 1);
    buffer& out = t(view, indexes);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(10.f * indexes.getPtr()[i]).epsilon(1e-5));
  }

  TEST_CASE("tableLookup: a sine table through inmap 0 1, wrapped, reads a sine (#1148)") {
    // One cycle of sin in 256 entries, read with a phase in 0-1: the
    // lookup a wavetable oscillator makes. Linear interpolation keeps the
    // error under the second-order bound (2 pi / 256)^2 / 8.
    constexpr UInt N = 256;
    auto b = sampleBuffer::create(1, N);
    std::vector<float> v(N);
    const double twoPi = 6.283185307179586;
    for (UInt n = 0; n < N; ++n)
      v[n] = static_cast<float>(std::sin(twoPi * n / N));
    b->setSamples(0, 0, v.data(), N);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.interp(interp::LINEAR).extend(edge::WRAP).inputMap(0.f, 1.f);
    CHECK(t.hasInputMap());
    buffer phases(YSE::STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
      phases.getPtr()[i] = static_cast<float>(i) / 37.f; // past 1 too
    buffer& out = t(view, phases);
    const double bound = (twoPi / N) * (twoPi / N) / 8.0 + 1e-6;
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(std::fabs(out.getPtr()[i] - std::sin(twoPi * phases.getPtr()[i])) < bound);
    }
  }

  TEST_CASE("tableLookup: the input map follows the table's length; equal ends clear it "
            "(#1148)") {
    auto b = Tens(10);
    sampleBufferReader reader(b);
    tableLookup t;
    t.inputMap(0.f, 1.f);
    {
      sampleView view = reader.pin();
      buffer x = Signal({0.f, 0.5f, 0.95f, 1.f});
      buffer& out = t(view, x);
      CHECK(out.getPtr()[0] == 0.f);
      CHECK(out.getPtr()[1] == 50.f);
      CHECK(out.getPtr()[2] == 90.f);
      CHECK(out.getPtr()[3] == 0.f); // one past the last: outside, zero
    }
    // A map from -1 to 1, as for a transfer function, and a longer table.
    b->setSize(20);
    std::vector<float> v(20);
    for (UInt n = 0; n < 20; ++n)
      v[n] = static_cast<float>(n);
    b->setSamples(0, 0, v.data(), 20);
    t.inputMap(-1.f, 1.f);
    {
      sampleView view = reader.pin();
      buffer x = Signal({-1.f, 0.f, 0.5f});
      buffer& out = t(view, x);
      CHECK(out.getPtr()[0] == 0.f);
      CHECK(out.getPtr()[1] == 10.f);
      CHECK(out.getPtr()[2] == 15.f);
    }
    // A reversed map reads the table backwards.
    t.inputMap(1.f, 0.f);
    {
      sampleView view = reader.pin();
      buffer x = Signal({0.5f, 0.75f});
      buffer& out = t(view, x);
      CHECK(out.getPtr()[0] == 10.f);
      CHECK(out.getPtr()[1] == 5.f);
    }
    t.inputMap(0.f, 0.f);
    CHECK_FALSE(t.hasInputMap());
    t.inputMap(2.f, std::numeric_limits<float>::infinity());
    CHECK_FALSE(t.hasInputMap());
    sampleView view = reader.pin();
    buffer three = Signal({3.f});
    CHECK(t(view, three).getPtr()[0] == 3.f);
  }

  TEST_CASE("tableLookup: the output scale maps the value range, unsigned or signed (#1148)") {
    auto b = Table({0.f, 127.f, 64.f, -128.f, 128.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.outputScale(0.f, 1.f);
    CHECK(t.hasOutputScale());
    // Unsigned, range 128: 0 to 127 becomes 0 to 1.
    CHECK(t.read(view, 0.f) == 0.f);
    CHECK(t.read(view, 1.f) == 1.f);
    CHECK(t.read(view, 2.f) == doctest::Approx(64.f / 127.f));
    // Signed: -128 to 128 becomes 0 to 1.
    t.signedRange(true);
    CHECK(t.read(view, 0.f) == 0.5f);
    CHECK(t.read(view, 3.f) == 0.f);
    CHECK(t.read(view, 4.f) == 1.f);
    // Another range and output range.
    t.signedRange(false).range(2).outputScale(-1.f, 1.f); // 0 to 1 becomes -1 to 1
    CHECK(t.read(view, 0.f) == -1.f);
    auto unit = Table({0.f, 0.25f, 1.f});
    sampleBufferReader unitReader(unit);
    sampleView unitView = unitReader.pin();
    CHECK(t.read(unitView, 1.f) == -0.5f);
    CHECK(t.read(unitView, 2.f) == 1.f);
    // Range 1, unsigned, is a single value: everything is the low end.
    t.range(0);
    CHECK(t.range() == 1);
    CHECK(t.read(unitView, 2.f) == -1.f);
    // Equal ends clear it.
    t.outputScale(0.f, 0.f);
    CHECK_FALSE(t.hasOutputScale());
    CHECK(t.read(unitView, 1.f) == 0.25f);
  }

  TEST_CASE("tableLookup: set, goto, next and prev move the position (#1148)") {
    auto b = Tens(8);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    // A new table starts as after a goto 0: the first next reads entry 0.
    CHECK(t(view, 4).getPtr()[3] == 0.f);
    CHECK(AfterNext(t, view) == 0.f);
    CHECK(AfterNext(t, view) == 10.f);
    CHECK(AfterNext(t, view) == 20.f);
    t.prev(view);
    CHECK(t(view, 1).getPtr()[0] == 10.f);

    // A number is the position; next adds one to it.
    t.set(view, 5.f);
    CHECK(t(view, 1).getPtr()[0] == 50.f);
    CHECK(AfterNext(t, view) == 60.f);
    CHECK(AfterNext(t, view) == 70.f);
    CHECK(AfterNext(t, view) == 0.f); // wraps to the start
    t.prev(view);
    CHECK(t.position(view) == 7.0); // below 0, wraps to the end
    CHECK(t(view, 1).getPtr()[0] == 70.f);

    // goto changes nothing until the next next or prev, which reads it.
    t.goTo(3.f);
    CHECK(t(view, 1).getPtr()[0] == 70.f);
    CHECK(AfterNext(t, view) == 30.f);
    CHECK(AfterNext(t, view) == 40.f);
    t.goTo(6.f);
    t.prev(view);
    CHECK(t(view, 1).getPtr()[0] == 60.f);
    // A number after a goto wins: the goto is no longer "just before".
    t.goTo(2.f);
    t.set(view, 1.f);
    CHECK(AfterNext(t, view) == 20.f);

    // The fraction is kept, and interpolation reads it.
    t.interp(interp::LINEAR);
    t.goTo(6.5f);
    CHECK(AfterNext(t, view) == 65.f);
    CHECK(AfterNext(t, view) == 35.f); // 7.5 is outside: halfway to 0 (zero)
    CHECK(t.position(view) == 7.5);
    CHECK(AfterNext(t, view) == 5.f); // 8.5 wraps to 0.5
    // The input map applies to a number, not to goto.
    t.interp(interp::NONE).inputMap(0.f, 1.f);
    t.set(view, 0.5f);
    CHECK(t(view, 1).getPtr()[0] == 40.f);
    CHECK(AfterNext(t, view) == 50.f);
    t.goTo(1.f);
    CHECK(AfterNext(t, view) == 10.f);
  }

  TEST_CASE("tableLookup: inputmode 1 steps on each trigger, by triggermode (#1148)") {
    auto b = Tens(4);
    sampleBufferReader reader(b);
    tableLookup t;
    t.mode(mode::INCREMENT);
    {
      sampleView view = reader.pin();
      // Zero to non-zero: 0 0 1 1 0 2 0 3 triggers at samples 2, 5, 7.
      buffer pulses = Signal({0.f, 0.f, 1.f, 1.f, 0.f, 2.f, 0.f, 3.f, 0.f, 4.f, 0.f, 5.f});
      buffer& out = t(view, pulses);
      const float expect[] = {0.f, 0.f, 0.f, 0.f, 0.f, 10.f, 10.f, 20.f, 20.f, 30.f, 30.f, 0.f};
      for (UInt i = 0; i < 12; ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == expect[i]);
      }
      // Change: every new value is a trigger, a repeat is not.
      t.trigger(trig::CHANGE);
      buffer values = Signal({5.f, 5.f, 6.f, 6.f, 6.f, 1.f, 0.f});
      buffer& changed = t(view, values);
      const float expect2[] = {0.f, 0.f, 10.f, 10.f, 10.f, 20.f, 30.f};
      for (UInt i = 0; i < 7; ++i) {
        CAPTURE(i);
        CHECK(changed.getPtr()[i] == expect2[i]);
      }
      // A number is a trigger too, tested against the input before it.
      t.trigger(trig::RISING);
      t.set(view, 0.f);
      t.set(view, 1.f);
      CHECK(t(view, 1).getPtr()[0] == 0.f); // from 3, wrapped
      t.set(view, 2.f); // not from 0: no trigger
      CHECK(t(view, 1).getPtr()[0] == 0.f);
    }
    // An edit to the entry read is heard between triggers.
    b->fill(7.f);
    sampleView after = reader.pin();
    CHECK(t(after, 1).getPtr()[0] == 7.f);
  }

  TEST_CASE("tableLookup: draws in proportion to the entries, never at 0 or below (#1148)") {
    // Weights 0, 3, -2, 1, NaN: only entries 1 and 3, about 3 to 1.
    auto b = Table({0.f, 3.f, -2.f, 1.f, std::numeric_limits<float>::quiet_NaN()});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.seed(1148);
    int counts[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 4000; ++i) {
      t.draw(view);
      const double p = t.position(view);
      REQUIRE(p >= 0.0);
      REQUIRE(p < 5.0);
      counts[static_cast<int>(p)]++;
    }
    CHECK(counts[0] == 0);
    CHECK(counts[2] == 0);
    CHECK(counts[4] == 0);
    CHECK(counts[1] + counts[3] == 4000);
    CHECK(counts[1] > 2 * counts[3]);
    CHECK(counts[1] < 4 * counts[3]);

    // Reproducible from a seed.
    tableLookup u;
    u.seed(1148);
    tableLookup w;
    w.seed(1148);
    for (int i = 0; i < 50; ++i) {
      u.draw(view);
      w.draw(view);
      CHECK(u.position(view) == w.position(view));
    }
    // A draw outputs the entry drawn, and clears a goto.
    t.goTo(0.f);
    t.draw(view);
    const float drawn = t(view, 1).getPtr()[0];
    CHECK((drawn == 3.f || drawn == 1.f));

    // No entry above 0: entry 0.
    auto none = Table({0.f, -1.f, 0.f});
    sampleBufferReader noneReader(none);
    sampleView noneView = noneReader.pin();
    t.set(noneView, 2.f);
    t.draw(noneView);
    CHECK(t.position(noneView) == 0.0);

    // inputmode 2 draws on each trigger.
    t.mode(mode::RANDOM).seed(7);
    auto single = Table({0.f, 0.f, 9.f, 0.f});
    sampleBufferReader singleReader(single);
    sampleView singleView = singleReader.pin();
    buffer pulse = Signal({0.f, 1.f, 1.f});
    buffer& out = t(singleView, pulse);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(out.getPtr()[1] == 9.f);
    CHECK(out.getPtr()[2] == 9.f);
  }

  TEST_CASE("tableLookup: no channel, no buffer or no samples read silence (#1148)") {
    auto b = Tens(4);
    sampleBufferReader reader(b);
    tableLookup t;
    t.extend(edge::EXTEND);
    buffer indexes = Signal({1.f, 2.f, 3.f});
    {
      sampleView view = reader.pin();
      t.channel(1);
      CHECK(t.channel() == 1);
      CHECK(t(view, indexes).getPtr()[2] == 0.f);
      CHECK(t(view, 3).getPtr()[2] == 0.f);
      t.next(view);
      t.draw(view);
      CHECK(t.read(view, 1.f) == 0.f);
    }
    t.channel(0);
    sampleBufferReader unbound;
    sampleView none = unbound.pin();
    CHECK(t(none, indexes).getPtr()[0] == 0.f);
    auto empty = sampleBuffer::create(1, 0);
    sampleBufferReader emptyReader(empty);
    sampleView nothing = emptyReader.pin();
    CHECK(t(nothing, indexes).getPtr()[1] == 0.f);
    t.mode(mode::INCREMENT);
    buffer pulse = Signal({0.f, 1.f});
    CHECK(t(nothing, pulse).getPtr()[1] == 0.f);
  }

  TEST_CASE("tableLookup: block lengths (#1148)") {
    auto b = Tens(8);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    tableLookup t;
    t.set(view, 3.f);
    buffer& out = t(view);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(out.getPtr()[YSE::STANDARD_BUFFERSIZE - 1] == 30.f);
    CHECK(t(view, 5).getLength() == 5);
    CHECK(t.out().getLength() == 5);
    buffer longer(YSE::STANDARD_BUFFERSIZE * 2);
    longer = 1.f;
    CHECK(t(view, longer).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(t(view, YSE::STANDARD_BUFFERSIZE * 3).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("tableLookup: no state follows the sample rate (#1148)") {
    SampleRateGuard guard;
    auto b = Tens(8);
    sampleBufferReader reader(b);
    tableLookup t;
    t.interp(interp::LINEAR).inputMap(0.f, 1.f);
    buffer quarter = Signal({0.25f});
    YSE::SAMPLERATE = 22050;
    {
      sampleView view = reader.pin();
      CHECK(t(view, quarter).getPtr()[0] == 20.f);
    }
    YSE::SAMPLERATE = 96000;
    sampleView view = reader.pin();
    CHECK(t(view, quarter).getPtr()[0] == 20.f);
    CHECK(t.read(view, 2.5f) == 25.f);
  }

  TEST_CASE("tableLookup: processing allocates nothing (#1148)") {
    auto b = Tens(1024);
    sampleBufferReader reader(b);
    tableLookup t;
    buffer indexes(YSE::STANDARD_BUFFERSIZE);
    buffer shorter(16);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
      indexes.getPtr()[i] = static_cast<float>(i % 3);
    shorter = 3.f;
    float last = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 6; ++block) {
        sampleView view = reader.pin();
        t.mode(static_cast<mode>(block % 3))
            .interp(static_cast<interp>(block % 3))
            .extend(static_cast<edge>(block % 4))
            .inputMap(0.f, block % 2 != 0 ? 1.f : 0.f)
            .outputScale(0.f, block % 2 != 0 ? 1.f : 0.f);
        t(view, indexes);
        t(view, shorter); // down and back up, within capacity
        t(view);
        t.set(view, 2.f);
        t.goTo(1.f);
        t.next(view);
        t.prev(view);
        t.draw(view);
        last = t(view, indexes).getPtr()[1];
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    (void)last;
  }

} // TEST_SUITE
