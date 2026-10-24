// Tests for YSE::DSP::sampleHoldMemory (YseEngine/dsp/generators/
// sampleHoldMemory.hpp), the class behind ~sash (issue #1083). The input is
// usually a counter (sample n reads n), so a stored value names the sample
// it was recorded at. Triggers are hand-made 0/1 pulses, or a DSP::phasor at
// 48 kHz: 375 Hz is exactly 128 samples per cycle (sample n reads
// (n mod 128) / 128), 93.75 Hz exactly 512.

#include <doctest/doctest.h>
#include <algorithm>
#include <limits>
#include <set>
#include <vector>
#include "dsp/generators/sampleHoldMemory.hpp"
#include "dsp/filters.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleHold;
using YSE::DSP::sampleHoldMemory;

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

  // Runs `fn` over the three signals in blocks of `block` samples.
  std::vector<float> Run(sampleHoldMemory& fn, const std::vector<float>& in,
                         const std::vector<float>& sample, const std::vector<float>& advance,
                         unsigned int block = N) {
    std::vector<float> out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const unsigned int length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer a(length);
      buffer b(length);
      buffer c(length);
      for (unsigned int i = 0; i < length; ++i) {
        a.getPtr()[i] = in[start + i];
        b.getPtr()[i] = sample[start + i];
        c.getPtr()[i] = advance[start + i];
      }
      buffer& result = fn(a, b, c);
      REQUIRE(result.getLength() == length);
      for (unsigned int i = 0; i < length; ++i)
        out.push_back(result.getPtr()[i]);
    }
    return out;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sampleHoldMemory: defaults, and with one trigger on both a plain sample and hold "
            "(#1083)") {
    sampleHoldMemory fn;
    CHECK(fn.size() == 1);
    CHECK(fn.maxSize() == sampleHoldMemory::DEFAULT_MAX_SIZE);
    CHECK(fn.writePosition() == 0);
    CHECK(fn.readPosition() == 0);
    CHECK(fn.value(0) == 0.f);

    // Size 1, level 0.5 on a 375 Hz phasor: the same as sampleHold rising
    // through 0.5, which takes sample 65 + 128k.
    const unsigned int total = 4 * N;
    const std::vector<float> in = Counter(total);
    const std::vector<float> clock = Ramp(total, 375.f);
    const std::vector<float> out = Run(fn, in, clock, clock);
    sampleHold reference;
    reference.triggerMode(sampleHold::ASCENDING).threshold(0.5f);
    std::vector<float> expected;
    for (unsigned int start = 0; start < total; start += N) {
      buffer x(N);
      buffer t(N);
      std::copy(in.begin() + start, in.begin() + start + N, x.getPtr());
      std::copy(clock.begin() + start, clock.begin() + start + N, t.getPtr());
      buffer& r = reference(x, t);
      expected.insert(expected.end(), r.getPtr(), r.getPtr() + N);
    }
    CHECK(out == expected);
    CHECK(out[64] == 0.f);
    CHECK(out[65] == 65.f);
    CHECK(out[192] == 65.f);
    CHECK(out[193] == 193.f);
  }

  TEST_CASE("sampleHoldMemory: level mode records and plays forward and backward (#1083)") {
    const unsigned int total = 200;
    const std::vector<float> in = Counter(total);
    const std::vector<float> record = Pulses(total, {10, 20, 30, 40, 100});
    const std::vector<float> advance = Pulses(total, {50, 60, 70, 80, 90, 110});

    sampleHoldMemory forward;
    forward.size(4);
    const std::vector<float> f = Run(forward, in, record, advance);
    CHECK(f[9] == 0.f);
    CHECK(f[10] == 10.f); // place 0, read at once
    CHECK(f[49] == 10.f);
    CHECK(f[50] == 20.f);
    CHECK(f[60] == 30.f);
    CHECK(f[70] == 40.f);
    CHECK(f[80] == 10.f); // round the end
    CHECK(f[90] == 20.f);
    CHECK(f[99] == 20.f);
    CHECK(f[110] == 30.f);
    // The fifth recording wrapped onto place 0.
    CHECK(forward.value(0) == 100.f);
    CHECK(forward.writePosition() == 1);

    sampleHoldMemory reverse;
    reverse.size(4).direction(sampleHoldMemory::REVERSE);
    const std::vector<float> r = Run(reverse, in, record, advance);
    CHECK(r[50] == 40.f); // 0 back to 3
    CHECK(r[60] == 30.f);
    CHECK(r[70] == 20.f);
    CHECK(r[80] == 10.f);
    CHECK(r[90] == 40.f);
    CHECK(r[100] == 40.f);
    CHECK(r[110] == 30.f);

    // The levels: a trigger must rise above them.
    sampleHoldMemory high;
    high.size(4).sampleLevel(1.f);
    const std::vector<float> h = Run(high, in, record, advance);
    CHECK(h[total - 1] == 0.f);
    CHECK(high.writePosition() == 0);
    sampleHoldMemory low;
    low.size(4).advanceLevel(2.f);
    Run(low, in, record, advance);
    CHECK(low.readPosition() == 0);
  }

  TEST_CASE("sampleHoldMemory: index mode reads the whole value, wrapped (#1083)") {
    sampleHoldMemory fn;
    const float values[] = {1.f, 2.f, 3.f, 4.f};
    fn.load(values, 4).mode(sampleHoldMemory::INDEX);
    CHECK(fn.size() == 4);
    CHECK(fn.process(0.f, 0.f, 0.f) == 1.f);
    CHECK(fn.process(0.f, 0.f, 2.9f) == 3.f);
    CHECK(fn.process(0.f, 0.f, 3.f) == 4.f);
    CHECK(fn.process(0.f, 0.f, 5.7f) == 2.f);
    CHECK(fn.process(0.f, 0.f, -1.f) == 4.f);
    CHECK(fn.process(0.f, 0.f, -0.5f) == 4.f);
    CHECK(fn.process(0.f, 0.f, -4.f) == 1.f);
    CHECK(fn.process(0.f, 0.f, 1.0e12f) == 1.f);
    CHECK(fn.readPosition() == 0);
    // Not finite: the place stays.
    fn.process(0.f, 0.f, 2.f);
    CHECK(fn.process(0.f, 0.f, std::numeric_limits<float>::quiet_NaN()) == 3.f);
    CHECK(fn.process(0.f, 0.f, std::numeric_limits<float>::infinity()) == 3.f);
  }

  TEST_CASE("sampleHoldMemory: subdivision mode plays the memory once per phasor cycle "
            "(#1083)") {
    const unsigned int total = 4 * N;
    const float values[] = {1.f, 2.f, 3.f, 4.f};
    const std::vector<float> zero(total, 0.f);
    for (const float hz : {375.f, -375.f}) {
      CAPTURE(hz);
      sampleHoldMemory fn;
      fn.load(values, 4).mode(sampleHoldMemory::SUBDIVISION);
      const std::vector<float> phase = Ramp(total, hz);
      const std::vector<float> out = Run(fn, zero, zero, phase);
      for (unsigned int n = 0; n < total; ++n) {
        CAPTURE(n);
        const auto place = static_cast<unsigned int>(phase[n] * 4.f);
        REQUIRE(out[n] == values[std::min(place, 3u)]);
      }
      if (hz > 0.f) {
        // Closed form: sample n reads (n mod 128) / 128.
        CHECK(out[0] == 1.f);
        CHECK(out[31] == 1.f);
        CHECK(out[32] == 2.f);
        CHECK(out[127] == 4.f);
        CHECK(out[128] == 1.f);
      }
    }
    // A phase outside 0..1 is wrapped.
    sampleHoldMemory fn;
    fn.load(values, 4).mode(sampleHoldMemory::SUBDIVISION);
    CHECK(fn.process(0.f, 0.f, 1.3f) == 2.f);
    CHECK(fn.process(0.f, 0.f, -0.1f) == 4.f);
  }

  TEST_CASE("sampleHoldMemory: a phasor records while a slower one plays the places in turn "
            "(#1083)") {
    // Recording on a 375 Hz phasor through 0.5: sample 65 + 128k goes to
    // place k mod 4. Playback: a 93.75 Hz phasor in four parts, place
    // floor((n mod 512) / 128).
    const unsigned int total = 8 * N;
    sampleHoldMemory fn;
    fn.size(4).mode(sampleHoldMemory::SUBDIVISION);
    const std::vector<float> out = Run(fn, Counter(total), Ramp(total, 375.f), Ramp(total, 93.75f));
    for (unsigned int n = 0; n < total; ++n) {
      CAPTURE(n);
      const unsigned int place = (n % 512) / 128;
      // The latest recording into `place` at or before n, 0 if none.
      float expected = 0.f;
      for (unsigned int k = place; 65 + 128 * k <= n; k += 4)
        expected = static_cast<float>(65 + 128 * k);
      REQUIRE(out[n] == expected);
    }
  }

  TEST_CASE("sampleHoldMemory: random and urn directions (#1083)") {
    const float values[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
    const auto advances = [](sampleHoldMemory& fn, unsigned int count) {
      std::vector<unsigned int> places;
      for (unsigned int i = 0; i < count; ++i) {
        fn.process(0.f, 0.f, 1.f);
        places.push_back(static_cast<unsigned int>(fn.process(0.f, 0.f, 0.f)));
      }
      return places;
    };

    sampleHoldMemory random;
    random.load(values, 8).direction(sampleHoldMemory::RANDOM).seed(1083);
    const std::vector<unsigned int> drawn = advances(random, 400);
    const std::set<unsigned int> seen(drawn.begin(), drawn.end());
    CHECK(seen.size() == 8);
    CHECK(*seen.rbegin() == 7);
    // With replacement: some place comes twice in a row.
    bool repeated = false;
    for (std::size_t i = 1; i < drawn.size(); ++i)
      repeated = repeated || drawn[i] == drawn[i - 1];
    CHECK(repeated);
    // The seed repeats the sequence.
    sampleHoldMemory again;
    again.load(values, 8).direction(sampleHoldMemory::RANDOM).seed(1083);
    CHECK(advances(again, 400) == drawn);

    sampleHoldMemory urn;
    urn.load(values, 8).direction(sampleHoldMemory::URN).seed(7);
    const std::vector<unsigned int> rounds = advances(urn, 80);
    for (std::size_t r = 0; r < 10; ++r) {
      CAPTURE(r);
      const std::set<unsigned int> round(rounds.begin() + static_cast<std::ptrdiff_t>(8 * r),
                                         rounds.begin() + static_cast<std::ptrdiff_t>(8 * r + 8));
      CHECK(round.size() == 8);
    }
    // Not just forward.
    const std::vector<unsigned int> inOrder = {1, 2, 3, 4, 5, 6, 7, 0};
    CHECK(std::vector<unsigned int>(rounds.begin(), rounds.begin() + 8) != inOrder);

    // A size change starts a new round in the new size.
    urn.size(3);
    const std::vector<unsigned int> small = advances(urn, 6);
    CHECK(std::set<unsigned int>(small.begin(), small.begin() + 3).size() == 3);
    CHECK(std::set<unsigned int>(small.begin() + 3, small.end()).size() == 3);
    CHECK(*std::max_element(small.begin(), small.end()) == 2);
  }

  TEST_CASE("sampleHoldMemory: load, size and maxSize (#1083)") {
    sampleHoldMemory fn(4);
    CHECK(fn.maxSize() == 4);

    // Finite values only, up to the capacity; the size follows.
    const float values[] = {1.f, std::numeric_limits<float>::quiet_NaN(), 2.f, 3.f, 4.f, 5.f};
    fn.load(values, 6);
    CHECK(fn.size() == 4);
    CHECK(fn.value(0) == 1.f);
    CHECK(fn.value(1) == 2.f);
    CHECK(fn.value(3) == 4.f);
    CHECK(fn.value(4) == 0.f); // past the capacity

    // Nothing finite, or nothing: no change.
    const float none[] = {std::numeric_limits<float>::infinity()};
    fn.load(none, 1).load(nullptr, 3).load(values, 0);
    CHECK(fn.size() == 4);
    CHECK(fn.value(0) == 1.f);

    // The size clamps to 1 .. capacity and wraps the positions; values stay.
    fn.mode(sampleHoldMemory::INDEX);
    fn.process(9.f, 1.f, 3.f); // records 9 at place 0, reads place 3
    CHECK(fn.writePosition() == 1);
    CHECK(fn.readPosition() == 3);
    fn.size(2);
    CHECK(fn.readPosition() == 1);
    CHECK(fn.writePosition() == 1);
    CHECK(fn.value(3) == 4.f);
    fn.size(0);
    CHECK(fn.size() == 1);
    CHECK(fn.readPosition() == 0);
    fn.size(99);
    CHECK(fn.size() == 4);

    // A load restarts the recording at place 0.
    fn.process(0.f, 0.f, 0.f);
    fn.process(7.f, 1.f, 0.f);
    CHECK(fn.writePosition() == 1);
    const float three[] = {5.f, 6.f, 7.f};
    fn.load(three, 3);
    CHECK(fn.size() == 3);
    CHECK(fn.writePosition() == 0);

    // maxSize clears and clamps.
    fn.maxSize(2);
    CHECK(fn.maxSize() == 2);
    CHECK(fn.size() == 2);
    CHECK(fn.value(0) == 0.f);
    fn.maxSize(0);
    CHECK(fn.maxSize() == 1);
    CHECK(fn.size() == 1);
    CHECK(sampleHoldMemory(0).maxSize() == 1);
  }

  TEST_CASE("sampleHoldMemory: non-finite input and triggers are skipped (#1083)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    sampleHoldMemory fn;
    fn.size(2);
    // A NaN input on a trigger: not recorded, the write position stays.
    CHECK(fn.process(nan, 1.f, 0.f) == 0.f);
    CHECK(fn.writePosition() == 0);
    // The NaN trigger in between is skipped: 1 -> NaN -> 1 is no rise.
    fn.process(0.f, 0.f, 0.f);
    fn.process(3.f, 1.f, 0.f);
    CHECK(fn.writePosition() == 1);
    fn.process(4.f, nan, 0.f);
    fn.process(4.f, 1.f, 0.f);
    CHECK(fn.writePosition() == 1);
    // And on the advance trigger.
    fn.process(0.f, 0.f, 1.f);
    CHECK(fn.readPosition() == 1);
    fn.process(0.f, 0.f, nan);
    fn.process(0.f, 0.f, 1.f);
    CHECK(fn.readPosition() == 1);
    // Non-finite levels are ignored.
    fn.sampleLevel(nan).advanceLevel(std::numeric_limits<float>::infinity());
    fn.process(0.f, 0.f, 0.f);
    fn.process(0.f, 0.f, 1.f);
    CHECK(fn.readPosition() == 0);
  }

  TEST_CASE("sampleHoldMemory: the block size does not change the output (#1083)") {
    const unsigned int total = 3 * N + 17;
    const std::vector<float> in = Counter(total);
    const std::vector<float> record = Ramp(total, 375.f);
    const std::vector<float> slow = Ramp(total, 93.75f);
    std::vector<float> index(total);
    for (unsigned int n = 0; n < total; ++n)
      index[n] = static_cast<float>(n / 50U) - 3.f; // NOLINT(bugprone-integer-division): steps
    for (int mode = sampleHoldMemory::LEVEL; mode <= sampleHoldMemory::SUBDIVISION; ++mode) {
      for (int dir = sampleHoldMemory::FORWARD; dir <= sampleHoldMemory::URN; ++dir) {
        CAPTURE(mode);
        CAPTURE(dir);
        const std::vector<float>& advance = mode == sampleHoldMemory::INDEX ? index : slow;
        const auto configure = [&](sampleHoldMemory& fn) {
          fn.size(5)
              .mode(static_cast<sampleHoldMemory::MODE>(mode))
              .direction(static_cast<sampleHoldMemory::DIRECTION>(dir))
              .advanceLevel(0.25f)
              .seed(42);
        };
        sampleHoldMemory whole;
        configure(whole);
        const std::vector<float> reference = Run(whole, in, record, advance);
        sampleHoldMemory single;
        configure(single);
        std::vector<float> perSample;
        perSample.reserve(total);
        for (unsigned int n = 0; n < total; ++n)
          perSample.push_back(single.process(in[n], record[n], advance[n]));
        CHECK(perSample == reference);
        for (const unsigned int block : {1u, 37u, 64u}) {
          CAPTURE(block);
          sampleHoldMemory fn;
          configure(fn);
          CHECK(Run(fn, in, record, advance, block) == reference);
        }
      }
    }
  }

  TEST_CASE("sampleHoldMemory: the shortest buffer sets the length, at most a standard block "
            "(#1083)") {
    sampleHoldMemory fn;
    buffer in(N);
    buffer sample(N / 2);
    buffer advance(N / 4);
    CHECK(fn(in, sample, advance).getLength() == N / 4);
    buffer big(2 * N);
    CHECK(fn(big, big, big).getLength() == N);
  }

  TEST_CASE("sampleHoldMemory: processing allocates nothing (#1083)") {
    sampleHoldMemory fn;
    buffer in(N);
    buffer sample(N);
    buffer advance(N);
    buffer shorter(N / 4);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      sample.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      advance.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    }
    const float values[] = {1.f, 2.f, 3.f};
    {
      TestHelpers::ProbeScope probe;
      for (int mode = sampleHoldMemory::LEVEL; mode <= sampleHoldMemory::SUBDIVISION; ++mode) {
        for (int dir = sampleHoldMemory::FORWARD; dir <= sampleHoldMemory::URN; ++dir) {
          fn.mode(static_cast<sampleHoldMemory::MODE>(mode))
              .direction(static_cast<sampleHoldMemory::DIRECTION>(dir))
              .size(static_cast<UInt>(dir + 3));
          fn(in, sample, advance);
          fn(shorter, sample, advance);
        }
      }
      fn.load(values, 3).sampleLevel(0.2f).advanceLevel(0.7f).seed(3);
      fn(in, sample, advance);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
