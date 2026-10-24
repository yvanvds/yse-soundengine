// Tests for YSE::DSP::signalStash (YseEngine/dsp/generators/signalStash.hpp),
// the class behind ~stash (issue #1084). The input is usually a counter
// (sample n reads n), so a stored value names the sample it was recorded at.
// Triggers are hand-made 0/1 pulses, or a DSP::phasor at 48 kHz: 375 Hz is
// exactly 128 samples per cycle (sample n reads (n mod 128) / 128).

#include <doctest/doctest.h>
#include <algorithm>
#include <limits>
#include <set>
#include <vector>
#include "dsp/generators/signalStash.hpp"
#include "dsp/filters.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleHold;
using YSE::DSP::signalStash;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // `samples` samples of a DSP::phasor at `hz`.
  std::vector<float> Ramp(unsigned int samples, float hz) {
    TestHelpers::ScopedSampleRate rate(48000);
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(hz, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  std::vector<float> Counter(unsigned int samples) {
    std::vector<float> result(samples);
    for (unsigned int n = 0; n < samples; ++n)
      result[n] = static_cast<float>(n);
    return result;
  }

  // 1 at the samples `at`, 0 elsewhere.
  std::vector<float> Pulses(unsigned int samples, const std::vector<unsigned int>& at) {
    std::vector<float> result(samples, 0.f);
    for (unsigned int n : at)
      result[n] = 1.f;
    return result;
  }

  // Runs `fn` over the four signals in blocks of `block` samples.
  std::vector<float> Run(signalStash& fn, const std::vector<float>& in,
                         const std::vector<float>& sample, const std::vector<float>& advance,
                         const std::vector<float>& write, unsigned int block = N) {
    std::vector<float> out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const unsigned int length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer a(length);
      buffer b(length);
      buffer c(length);
      buffer d(length);
      for (unsigned int i = 0; i < length; ++i) {
        a.getPtr()[i] = in[start + i];
        b.getPtr()[i] = sample[start + i];
        c.getPtr()[i] = advance[start + i];
        d.getPtr()[i] = write[start + i];
      }
      buffer& result = fn(a, b, c, d);
      REQUIRE(result.getLength() == length);
      for (unsigned int i = 0; i < length; ++i)
        out.push_back(result.getPtr()[i]);
    }
    return out;
  }

  // One sample with the advance trigger as the read index.
  float Read(signalStash& fn, float index) {
    return fn.process(0.f, 0.f, index, 0.f);
  }

  signalStash Table(std::initializer_list<float> values) {
    signalStash fn;
    const std::vector<float> v(values);
    fn.load(v.data(), static_cast<unsigned int>(v.size()));
    return fn;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("signalStash: defaults (#1084)") {
    signalStash fn;
    CHECK(fn.size() == 1);
    CHECK(fn.maxSize() == signalStash::DEFAULT_MAX_SIZE);
    CHECK(fn.writePosition() == 0);
    CHECK(fn.readPosition() == 0);
    CHECK(fn.value(0) == 0.f);
    CHECK(fn.value(5000) == 0.f);
  }

  TEST_CASE("signalStash: with size 1 and one trigger on both, sampleHold in every trigger mode "
            "and duration (#1084)") {
    const unsigned int total = 4 * N + 37;
    const std::vector<float> in = Counter(total);
    // A rising and a falling 375 Hz phasor, and a 0/1 pulse train.
    const std::vector<float> rising = Ramp(total, 375.f);
    const std::vector<float> falling = Ramp(total, -375.f);
    std::vector<float> pulses(total, 0.f);
    for (unsigned int n = 0; n < total; ++n)
      pulses[n] = (n / 7) % 3 == 0 ? 1.f : 0.f;
    const std::vector<const std::vector<float>*> clocks = {&rising, &falling, &pulses};
    const float levels[] = {0.5f, 0.f, 1.f, 0.25f};
    const unsigned int durations[] = {0, 1, 5};

    for (int m = signalStash::ASCENDING; m <= signalStash::PHASE; ++m) {
      for (const std::vector<float>* clock : clocks) {
        for (float level : levels) {
          for (unsigned int hold : durations) {
            CAPTURE(m);
            CAPTURE(level);
            CAPTURE(hold);
            signalStash fn;
            fn.sampleTriggerMode(static_cast<signalStash::TRIGGER_MODE>(m))
                .advanceTriggerMode(static_cast<signalStash::TRIGGER_MODE>(m))
                .sampleLevel(level)
                .advanceLevel(level)
                .duration(hold);
            const std::vector<float> out = Run(fn, in, *clock, *clock, in);

            sampleHold reference;
            reference.triggerMode(static_cast<sampleHold::TRIGGER_MODE>(m))
                .threshold(level)
                .duration(hold)
                .reset(0.f);
            std::vector<float> expected;
            for (unsigned int start = 0; start < total; start += N) {
              const unsigned int length = std::min(N, total - start);
              buffer x(length);
              buffer t(length);
              std::copy(in.begin() + start, in.begin() + start + length, x.getPtr());
              std::copy(clock->begin() + start, clock->begin() + start + length, t.getPtr());
              buffer& r = reference(x, t);
              expected.insert(expected.end(), r.getPtr(), r.getPtr() + length);
            }
            CHECK(out == expected);
          }
        }
      }
    }

    // Spot checks of the default against the closed form: rising through
    // 0.5 takes sample 65 + 128k.
    signalStash fn;
    const std::vector<float> out = Run(fn, in, rising, rising, in);
    CHECK(out[64] == 0.f);
    CHECK(out[65] == 65.f);
    CHECK(out[192] == 65.f);
    CHECK(out[193] == 193.f);
  }

  TEST_CASE("signalStash: next mode records and plays forward, backward, random and urn "
            "(#1084)") {
    const unsigned int total = 200;
    const std::vector<float> in = Counter(total);
    const std::vector<float> record = Pulses(total, {10, 20, 30, 40, 100});
    const std::vector<float> advance = Pulses(total, {50, 60, 70, 80, 90, 110});
    const std::vector<float> zero(total, 0.f);

    signalStash forward;
    forward.size(4);
    std::vector<float> out = Run(forward, in, record, advance, zero);
    // Places 0..3 hold 10, 20, 30, 40; 100 wraps into place 0. Reading starts
    // at place 0 and each advance moves one on.
    CHECK(out[9] == 0.f);
    CHECK(out[10] == 10.f);
    CHECK(out[49] == 10.f);
    CHECK(out[50] == 20.f);
    CHECK(out[60] == 30.f);
    CHECK(out[70] == 40.f);
    CHECK(out[80] == 10.f);
    CHECK(out[90] == 20.f);
    CHECK(out[100] == 20.f);
    CHECK(out[110] == 30.f);
    CHECK(forward.writePosition() == 1);
    CHECK(forward.value(0) == 100.f);

    signalStash backward;
    backward.size(4).direction(signalStash::REVERSE);
    out = Run(backward, in, record, advance, zero);
    CHECK(out[50] == 40.f);
    CHECK(out[60] == 30.f);
    CHECK(out[70] == 20.f);
    CHECK(out[80] == 10.f);
    CHECK(out[90] == 40.f);
    CHECK(out[100] == 40.f);
    CHECK(out[110] == 30.f);
    CHECK(backward.value(0) == 100.f);

    // Random with replacement, repeatable from a seed; urn plays each place
    // once per round, a size change starting a new round.
    const auto places = [](signalStash& fn, unsigned int count) {
      std::vector<unsigned int> result;
      for (unsigned int i = 0; i < count; ++i) {
        fn.advance();
        result.push_back(fn.readPosition());
      }
      return result;
    };
    signalStash random;
    random.size(8).direction(signalStash::RANDOM).seed(7);
    const std::vector<unsigned int> first = places(random, 64);
    random.seed(7);
    CHECK(places(random, 64) == first);
    CHECK(std::set<unsigned int>(first.begin(), first.end()).size() > 1);
    for (unsigned int p : first)
      CHECK(p < 8);

    signalStash urn;
    urn.size(8).direction(signalStash::URN);
    for (int round = 0; round < 4; ++round) {
      const std::vector<unsigned int> one = places(urn, 8);
      CHECK(std::set<unsigned int>(one.begin(), one.end()).size() == 8);
    }
    places(urn, 3);
    urn.size(5);
    for (int round = 0; round < 3; ++round) {
      const std::vector<unsigned int> one = places(urn, 5);
      CHECK(std::set<unsigned int>(one.begin(), one.end()).size() == 5);
      for (unsigned int p : one)
        CHECK(p < 5);
    }
  }

  TEST_CASE("signalStash: index reads under every interpolation and range (#1084)") {
    // Places 0..3 hold 1, 2, 4, 8.
    signalStash fn = Table({1.f, 2.f, 4.f, 8.f});
    fn.mode(signalStash::INDEX);

    // Truncate (the default) and wrap (the default).
    CHECK(Read(fn, 0.f) == 1.f);
    CHECK(Read(fn, 1.9f) == 2.f);
    CHECK(Read(fn, 3.f) == 8.f);
    CHECK(Read(fn, 4.f) == 1.f);
    CHECK(Read(fn, 6.5f) == 4.f);
    CHECK(Read(fn, -1.f) == 8.f);
    CHECK(Read(fn, -0.5f) == 8.f);
    CHECK(fn.readPosition() == 3);

    fn.interpolation(signalStash::ROUND);
    CHECK(Read(fn, 1.4f) == 2.f);
    CHECK(Read(fn, 1.5f) == 4.f);
    CHECK(Read(fn, 3.5f) == 1.f); // 4, wrapped
    CHECK(Read(fn, -0.4f) == 1.f);

    fn.interpolation(signalStash::LINEAR);
    CHECK(Read(fn, 0.5f) == doctest::Approx(1.5f));
    CHECK(Read(fn, 2.25f) == doctest::Approx(5.f));
    CHECK(fn.readPosition() == 2);
    CHECK(Read(fn, 3.5f) == doctest::Approx(4.5f)); // the last place mixes on to the first
    CHECK(Read(fn, -0.5f) == doctest::Approx(4.5f));
    CHECK(Read(fn, 5.75f) == doctest::Approx(3.5f));

    fn.range(signalStash::EXTEND);
    CHECK(Read(fn, 3.5f) == 8.f);
    CHECK(Read(fn, -2.f) == 1.f);
    CHECK(Read(fn, 2.5f) == doctest::Approx(6.f));
    fn.interpolation(signalStash::TRUNCATE);
    CHECK(Read(fn, 10.f) == 8.f);
    CHECK(Read(fn, -0.5f) == 1.f);

    fn.range(signalStash::ZERO);
    CHECK(Read(fn, 2.f) == 4.f);
    CHECK(Read(fn, 4.f) == 0.f);
    CHECK(Read(fn, -0.5f) == 0.f);
    fn.interpolation(signalStash::LINEAR);
    CHECK(Read(fn, 2.5f) == doctest::Approx(6.f));
    CHECK(Read(fn, 3.f) == 8.f);
    CHECK(Read(fn, 3.25f) == 0.f);

    // Keep: out of range changes nothing.
    fn.range(signalStash::KEEP);
    CHECK(Read(fn, 1.f) == 2.f);
    CHECK(Read(fn, 9.f) == 2.f);
    CHECK(Read(fn, -1.f) == 2.f);
    CHECK(Read(fn, 0.5f) == doctest::Approx(1.5f));
    CHECK(Read(fn, 3.5f) == doctest::Approx(1.5f));

    // Not a finite number: no change.
    CHECK(Read(fn, std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(1.5f));
    CHECK(Read(fn, std::numeric_limits<float>::infinity()) == doctest::Approx(1.5f));
  }

  TEST_CASE("signalStash: normalized reads, and a phasor plays the memory once per cycle "
            "(#1084)") {
    signalStash two = Table({5.f, 7.f});
    two.mode(signalStash::NORMALIZED);
    CHECK(Read(two, 0.f) == 5.f);
    CHECK(Read(two, 0.49f) == 5.f);
    CHECK(Read(two, 0.5f) == 7.f);
    CHECK(Read(two, 0.99f) == 7.f);
    CHECK(Read(two, 1.f) == 5.f); // 2, wrapped
    CHECK(Read(two, -0.25f) == 7.f); // -0.5 rounds down to -1, wrapped
    two.interpolation(signalStash::LINEAR);
    CHECK(Read(two, 0.25f) == doctest::Approx(6.f));
    CHECK(Read(two, 0.75f) == doctest::Approx(6.f)); // the last place mixes on to the first

    // A 375 Hz phasor through a list of four: 32 samples a place.
    const unsigned int total = 3 * N;
    const std::vector<float> clock = Ramp(total, 375.f);
    const std::vector<float> zero(total, 0.f);
    signalStash fn = Table({10.f, 20.f, 30.f, 40.f});
    fn.mode(signalStash::NORMALIZED);
    const std::vector<float> out = Run(fn, zero, zero, clock, zero);
    for (unsigned int n = 0; n < total; ++n) {
      CAPTURE(n);
      const unsigned int place = (n % 128) / 32;
      CHECK(out[n] == 10.f * static_cast<float>(place + 1));
    }
  }

  TEST_CASE("signalStash: an audio-rate step sequencer, advanced by a phasor's resets (#1084)") {
    // Advance trigger mode PHASE with level 0: a 375 Hz phasor passes phase 0
    // as it leaves it, at sample 1 + 128k, read as sampleHold and phasorWhat
    // read a phase (a step onto 0 has not passed it yet). Each pass moves one
    // place on through a list of three.
    const unsigned int total = 6 * 128;
    const std::vector<float> clock = Ramp(total, 375.f);
    const std::vector<float> zero(total, 0.f);
    signalStash fn = Table({3.f, 5.f, 9.f});
    fn.advanceTriggerMode(signalStash::PHASE).advanceLevel(0.f);
    const std::vector<float> out = Run(fn, zero, zero, clock, zero);
    const float sequence[] = {3.f, 5.f, 9.f};
    for (unsigned int n = 0; n < total; ++n) {
      CAPTURE(n);
      CHECK(out[n] == sequence[((n + 127) / 128) % 3]);
    }

    // With duration 1 the steps are impulses, one per pass.
    signalStash impulses = Table({3.f, 5.f, 9.f});
    impulses.advanceTriggerMode(signalStash::PHASE).advanceLevel(0.f).duration(1);
    const std::vector<float> pulses = Run(impulses, zero, zero, clock, zero);
    for (unsigned int n = 0; n < total; ++n) {
      CAPTURE(n);
      const float expected = n % 128 == 1 ? sequence[((n + 127) / 128) % 3] : 0.f;
      CHECK(pulses[n] == expected);
    }
  }

  TEST_CASE("signalStash: a duration in the index modes starts at each change of value "
            "(#1084)") {
    signalStash fn = Table({1.f, 2.f});
    fn.mode(signalStash::INDEX).duration(2);
    // Before any change: 0.
    CHECK(Read(fn, 0.f) == 1.f); // 0 -> 1 is a change
    CHECK(Read(fn, 0.f) == 1.f);
    CHECK(Read(fn, 0.f) == 0.f);
    CHECK(Read(fn, 1.f) == 2.f);
    CHECK(Read(fn, 1.f) == 2.f);
    CHECK(Read(fn, 1.f) == 0.f);
    CHECK(Read(fn, 3.f) == 0.f); // place 1 again, no change
    CHECK(Read(fn, 2.f) == 1.f);
    fn.duration(0);
    CHECK(Read(fn, 2.f) == 1.f);
  }

  TEST_CASE("signalStash: index and normalized writes, and the write position (#1084)") {
    signalStash fn;
    fn.size(4).writeMode(signalStash::INDEX);
    // Record on every sample, at the place the write index names.
    const auto record = [&fn](float in, float index) { fn.process(in, 1.f, 0.f, index); };
    fn.sampleTriggerMode(signalStash::EQUALS).sampleLevel(1.f);
    record(11.f, 1.f);
    record(13.f, 3.7f); // rounds down
    record(10.f, 4.f); // wraps to 0
    record(12.f, -2.f); // wraps to 2
    CHECK(fn.value(0) == 10.f);
    CHECK(fn.value(1) == 11.f);
    CHECK(fn.value(2) == 12.f);
    CHECK(fn.value(3) == 13.f);
    CHECK(fn.writePosition() == 0); // index writes leave it

    fn.interpolation(signalStash::ROUND);
    record(21.f, 0.6f); // rounds to 1
    CHECK(fn.value(1) == 21.f);
    fn.interpolation(signalStash::LINEAR);
    record(22.f, 2.9f); // a write rounds down
    CHECK(fn.value(2) == 22.f);

    fn.range(signalStash::ZERO);
    record(99.f, 4.f);
    record(99.f, -1.f);
    fn.range(signalStash::KEEP);
    record(99.f, 7.f);
    CHECK(fn.value(0) == 10.f);
    CHECK(fn.value(3) == 13.f);
    fn.range(signalStash::EXTEND);
    record(31.f, 7.f);
    record(30.f, -3.f);
    CHECK(fn.value(3) == 31.f);
    CHECK(fn.value(0) == 30.f);
    // Not a finite index or input: nothing written.
    record(98.f, std::numeric_limits<float>::quiet_NaN());
    record(std::numeric_limits<float>::infinity(), 1.f);
    CHECK(fn.value(1) == 21.f);

    // Normalized: the index as a share of the size.
    fn.writeMode(signalStash::NORMALIZED)
        .range(signalStash::WRAP)
        .interpolation(signalStash::TRUNCATE);
    record(40.f, 0.f);
    record(41.f, 0.25f);
    record(42.f, 0.74f);
    record(43.f, 1.75f); // 7, wrapped to 3
    CHECK(fn.value(0) == 40.f);
    CHECK(fn.value(1) == 41.f);
    CHECK(fn.value(2) == 42.f);
    CHECK(fn.value(3) == 43.f);

    // write() writes at the write position, which writePosition() and
    // advanceWrite() move, read as the write mode reads an index.
    fn.writeMode(signalStash::NEXT);
    fn.writePosition(2.f).write(50.f);
    CHECK(fn.writePosition() == 2);
    CHECK(fn.value(2) == 50.f);
    fn.advanceWrite().write(51.f);
    CHECK(fn.value(3) == 51.f);
    fn.advanceWrite();
    CHECK(fn.writePosition() == 0);
    fn.writePosition(-1.f);
    CHECK(fn.writePosition() == 3);
    fn.writeMode(signalStash::NORMALIZED).writePosition(0.5f);
    CHECK(fn.writePosition() == 2);
    fn.range(signalStash::ZERO).writePosition(1.5f);
    CHECK(fn.writePosition() == 2);
    fn.writePosition(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.writePosition() == 2);
    fn.write(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.value(2) == 50.f);

    // A next-mode recording writes at the write position and moves it on.
    fn.writeMode(signalStash::NEXT).sampleTriggerMode(signalStash::ASCENDING).sampleLevel(0.5f);
    fn.process(0.f, 0.f, 0.f, 0.f);
    fn.process(60.f, 1.f, 0.f, 0.f);
    CHECK(fn.value(2) == 60.f);
    CHECK(fn.writePosition() == 3);
  }

  TEST_CASE("signalStash: capture records the next sample, advance moves the output (#1084)") {
    signalStash fn;
    fn.size(3);
    // No trigger: nothing recorded.
    CHECK(fn.process(7.f, 0.f, 0.f, 0.f) == 0.f);
    fn.capture();
    CHECK(fn.process(8.f, 0.f, 0.f, 0.f) == 8.f); // recorded at place 0, read there
    CHECK(fn.process(9.f, 0.f, 0.f, 0.f) == 8.f); // once only
    CHECK(fn.writePosition() == 1);
    fn.capture();
    fn.process(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 0.f); // not recorded
    CHECK(fn.writePosition() == 1);

    fn.capture();
    fn.process(10.f, 0.f, 0.f, 0.f);
    fn.advance();
    CHECK(fn.readPosition() == 1);
    CHECK(fn.process(0.f, 0.f, 0.f, 0.f) == 10.f);

    // With a duration, advance() starts one.
    fn.duration(2);
    fn.advance();
    CHECK(fn.process(0.f, 0.f, 0.f, 0.f) == 0.f); // place 2 holds 0
    fn.advance();
    CHECK(fn.process(0.f, 0.f, 0.f, 0.f) == 8.f);
    CHECK(fn.process(0.f, 0.f, 0.f, 0.f) == 8.f);
    CHECK(fn.process(0.f, 0.f, 0.f, 0.f) == 0.f);
  }

  TEST_CASE("signalStash: load, size and maxSize (#1084)") {
    signalStash fn(4);
    CHECK(fn.maxSize() == 4);
    const float values[] = {1.f, std::numeric_limits<float>::quiet_NaN(), 2.f, 3.f, 4.f, 5.f};
    fn.load(values, 6);
    CHECK(fn.size() == 4);
    CHECK(fn.value(0) == 1.f);
    CHECK(fn.value(1) == 2.f);
    CHECK(fn.value(3) == 4.f);
    CHECK(fn.writePosition() == 0);
    fn.load(nullptr, 3);
    const float none[] = {std::numeric_limits<float>::quiet_NaN()};
    fn.load(none, 1);
    CHECK(fn.size() == 4);

    fn.size(9);
    CHECK(fn.size() == 4);
    fn.size(0);
    CHECK(fn.size() == 1);
    fn.size(3);
    CHECK(fn.value(2) == 3.f); // values are kept

    fn.writePosition(2.f).advance().advance();
    fn.size(2);
    CHECK(fn.writePosition() == 0);
    CHECK(fn.readPosition() == 0);

    fn.maxSize(0);
    CHECK(fn.maxSize() == 1);
    CHECK(fn.size() == 1);
    CHECK(fn.value(0) == 0.f);
    fn.maxSize(16).size(16);
    CHECK(fn.size() == 16);
    fn.maxSize(8);
    CHECK(fn.size() == 8);
  }

  TEST_CASE("signalStash: non-finite settings and triggers are skipped (#1084)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    signalStash fn;
    fn.size(2).sampleLevel(nan).advanceLevel(nan);
    fn.sampleTriggerMode(static_cast<signalStash::TRIGGER_MODE>(9))
        .advanceTriggerMode(static_cast<signalStash::TRIGGER_MODE>(-1))
        .direction(static_cast<signalStash::DIRECTION>(4))
        .mode(static_cast<signalStash::MODE>(3))
        .writeMode(static_cast<signalStash::MODE>(-1))
        .range(static_cast<signalStash::RANGE>(4))
        .interpolation(static_cast<signalStash::INTERPOLATION>(3));
    // Still the defaults: a rise through 0.5 records, a rise advances.
    fn.process(1.f, 0.f, 0.f, 0.f);
    fn.process(2.f, nan, nan, 0.f); // skipped
    CHECK(fn.process(3.f, 1.f, 0.f, 0.f) == 3.f);
    CHECK(fn.process(4.f, nan, 1.f, 0.f) == 0.f); // advanced to place 1
    CHECK(fn.process(5.f, 0.f, nan, 0.f) == 0.f);
    CHECK(fn.process(6.f, 1.f, 0.f, 0.f) == 6.f); // place 1 recorded, read
    CHECK(fn.value(0) == 3.f);
  }

  TEST_CASE("signalStash: the block size does not change the output (#1084)") {
    const unsigned int total = 3 * N + 17;
    const std::vector<float> in = Counter(total);
    const std::vector<float> record = Ramp(total, 375.f);
    const std::vector<float> advance = Ramp(total, -93.75f);
    std::vector<float> write(total);
    for (unsigned int n = 0; n < total; ++n)
      write[n] = static_cast<float>(n % 11) * 0.7f;
    for (int m = signalStash::NEXT; m <= signalStash::NORMALIZED; ++m) {
      for (int w = signalStash::NEXT; w <= signalStash::NORMALIZED; ++w) {
        for (int d = signalStash::FORWARD; d <= signalStash::URN; ++d) {
          CAPTURE(m);
          CAPTURE(w);
          CAPTURE(d);
          const auto make = [&]() {
            signalStash fn;
            fn.size(6)
                .mode(static_cast<signalStash::MODE>(m))
                .writeMode(static_cast<signalStash::MODE>(w))
                .direction(static_cast<signalStash::DIRECTION>(d))
                .interpolation(signalStash::LINEAR)
                .duration(3);
            return fn;
          };
          signalStash whole = make();
          signalStash pieces = make();
          CHECK(Run(whole, in, record, advance, write) ==
                Run(pieces, in, record, advance, write, 13));
        }
      }
    }
  }

  TEST_CASE("signalStash: the shortest buffer sets the length, at most a standard block "
            "(#1084)") {
    signalStash fn;
    buffer in(N);
    buffer sample(N);
    buffer advance(40);
    buffer write(N);
    CHECK(fn(in, sample, advance, write).getLength() == 40);
    buffer shortWrite(12);
    buffer full(N);
    CHECK(fn(in, sample, full, shortWrite).getLength() == 12);
    buffer big(2 * N);
    CHECK(fn(big, big, big, big).getLength() == N);
  }

  TEST_CASE("signalStash: processing allocates nothing (#1084)") {
    signalStash fn;
    buffer in(N);
    buffer sample(N);
    buffer advance(N);
    buffer write(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      sample.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      advance.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
      write.getPtr()[i] = static_cast<float>(i % 7);
    }
    const float values[] = {1.f, 2.f, 3.f};
    fn(in, sample, advance, write);
    {
      TestHelpers::ProbeScope probe;
      for (int mode = signalStash::NEXT; mode <= signalStash::NORMALIZED; ++mode) {
        for (int dir = signalStash::FORWARD; dir <= signalStash::URN; ++dir) {
          fn.mode(static_cast<signalStash::MODE>(mode))
              .writeMode(static_cast<signalStash::MODE>(mode))
              .direction(static_cast<signalStash::DIRECTION>(dir))
              .size(8)
              .duration(2)
              .interpolation(signalStash::LINEAR);
          fn(in, sample, advance, write);
          fn.load(values, 3).write(4.f).writePosition(1.f).advanceWrite().advance().capture();
          fn(in, sample, advance, write);
        }
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
