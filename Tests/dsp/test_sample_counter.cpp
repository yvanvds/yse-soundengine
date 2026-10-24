// Tests for YSE::DSP::sampleCounter (YseEngine/dsp/generators/sampleCounter.hpp),
// issue #1066. Reference output is closed form: the count at sample n after a
// start is minimum + n, or minimum + n % (limit - minimum) with a limit, and
// the carry is n / (limit - minimum).

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <vector>
#include "dsp/generators/sampleCounter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleCounter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  bool Constant(buffer& block, float value) {
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != value) return false;
    return true;
  }

  // The first samples of `block` are `expected`.
  void CheckHead(buffer& block, const std::vector<float>& expected) {
    REQUIRE(block.getLength() >= expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(block.getPtr()[i] == expected[i]);
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sampleCounter: a new counter is stopped at its minimum (#1066)") {
    sampleCounter counter;
    CHECK_FALSE(counter.counting());
    CHECK(counter.minimum() == 0);
    CHECK(counter.limit() == 0);
    buffer& out = counter();
    CHECK(out.getLength() == N);
    CHECK(Constant(out, 0.f));
    CHECK(Constant(counter.carry(), 0.f));
    // While stopped the output follows the minimum.
    counter.minimum(5);
    CHECK(Constant(counter(), 5.f));
    CHECK(counter.count() == 5);
  }

  TEST_CASE("sampleCounter: start counts up by 1 per sample, across blocks (#1066)") {
    sampleCounter counter;
    counter.minimum(-3).start();
    CHECK(counter.counting());
    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      buffer& out = counter();
      for (unsigned int i = 0; i < N; ++i)
        REQUIRE(out.getPtr()[i] == static_cast<float>(-3 + static_cast<int>(block * N + i)));
      CHECK(Constant(counter.carry(), 0.f));
    }
    CHECK(counter.count() == -3 + 3 * static_cast<std::int64_t>(N));
    // start(value) sets the minimum and restarts there.
    counter.start(100);
    CHECK(counter.minimum() == 100);
    CheckHead(counter(), {100.f, 101.f, 102.f});
  }

  TEST_CASE("sampleCounter: a limit loops the count and the carry counts the wraps (#1066)") {
    for (const auto& range :
         std::vector<std::pair<int, int>>{{0, 4}, {3, 7}, {-5, 2}, {0, 1}, {10, 1000}}) {
      CAPTURE(range.first);
      CAPTURE(range.second);
      sampleCounter counter;
      counter.minimum(range.first).limit(range.second).start();
      const int span = range.second - range.first;
      for (unsigned int block = 0; block < 4; ++block) {
        buffer& out = counter();
        buffer& carry = counter.carry();
        for (unsigned int i = 0; i < N; ++i) {
          const int n = static_cast<int>(block * N + i);
          REQUIRE(out.getPtr()[i] == static_cast<float>(range.first + n % span));
          const int wraps = n / span; // whole wraps: integer division on purpose
          REQUIRE(carry.getPtr()[i] == static_cast<float>(wraps));
        }
      }
    }
  }

  TEST_CASE("sampleCounter: a minimum at or above the limit wraps at every sample (#1066)") {
    sampleCounter counter;
    counter.minimum(10).limit(4).start();
    buffer& out = counter();
    CHECK(Constant(out, 10.f));
    CheckHead(counter.carry(), {1.f, 2.f, 3.f, 4.f});
  }

  TEST_CASE("sampleCounter: stop goes back to the minimum, a restart clears the carry (#1066)") {
    sampleCounter counter;
    counter.minimum(2).limit(4).start();
    counter();
    CHECK(counter.carries() > 0);
    counter.stop();
    CHECK_FALSE(counter.counting());
    CHECK(counter.carries() == 0);
    CHECK(Constant(counter(), 2.f));
    CHECK(Constant(counter.carry(), 0.f));
    counter.start();
    CheckHead(counter(), {2.f, 3.f, 2.f, 3.f});
    CheckHead(counter.carry(), {0.f, 0.f, 1.f, 1.f});
  }

  TEST_CASE("sampleCounter: a new minimum or limit does not restart the count (#1066)") {
    sampleCounter counter;
    counter.limit(1000).start();
    CheckHead(counter(4), {0.f, 1.f, 2.f, 3.f});
    // A new minimum: the count goes on.
    counter.minimum(2);
    CheckHead(counter(4), {4.f, 5.f, 6.f, 7.f});
    CheckHead(counter.carry(), {0.f, 0.f, 0.f, 0.f});
    // A limit below the count: the next sample wraps, to the new minimum.
    counter.limit(6);
    CheckHead(counter(5), {2.f, 3.f, 4.f, 5.f, 2.f});
    CheckHead(counter.carry(), {1.f, 1.f, 1.f, 1.f, 2.f});
  }

  TEST_CASE("sampleCounter: an enable signal starts and stops it sample by sample (#1066)") {
    sampleCounter counter;
    counter.minimum(10);
    buffer enable(8);
    const float gate[] = {0.f, 0.f, 1.f, 1.f, 0.5f, 0.f, -1.f, 1.f};
    for (unsigned int i = 0; i < 8; ++i)
      enable.getPtr()[i] = gate[i];
    buffer& out = counter(enable);
    CHECK(out.getLength() == 8);
    CHECK(counter.carry().getLength() == 8);
    CheckHead(out, {10.f, 10.f, 10.f, 11.f, 12.f, 10.f, 10.f, 11.f});
    CHECK(counter.counting());

    // Still on at the next block: it counts on.
    enable = 1.f;
    CheckHead(counter(enable), {12.f, 13.f});

    // A NaN sample counts as 0.
    enable.getPtr()[0] = std::numeric_limits<float>::quiet_NaN();
    CheckHead(counter(enable), {10.f, 10.f, 11.f});

    // The signal decides over a stop at the top of the block.
    counter.stop();
    enable = 1.f;
    CheckHead(counter(enable), {10.f, 11.f});
    // A zero signal over a start.
    counter.start();
    enable = 0.f;
    CHECK(Constant(counter(enable), 10.f));
    CHECK_FALSE(counter.counting());
  }

  TEST_CASE("sampleCounter: the count is 64-bit, past the int range (#1066)") {
    sampleCounter counter;
    counter.start(std::numeric_limits<int>::max() - 1);
    buffer& out = counter(4);
    CHECK(out.getLength() == 4);
    CHECK(counter.count() == static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 3);
    CHECK(out.getPtr()[3] > 2.1e9f);
  }

  TEST_CASE("sampleCounter: a block is at most STANDARD_BUFFERSIZE long (#1066)") {
    sampleCounter counter;
    counter.start();
    CHECK(counter(N * 2).getLength() == N);
    CHECK(counter(16).getLength() == 16);
    CHECK(counter.carry().getLength() == 16);
  }

  TEST_CASE("sampleCounter: the same output at any sample rate (#1066)") {
    std::vector<float> reference;
    for (unsigned int rate : {22050u, 44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      sampleCounter counter;
      counter.minimum(7).limit(40).start();
      buffer& out = counter();
      if (reference.empty()) {
        reference.assign(out.getPtr(), out.getPtr() + out.getLength());
      } else {
        for (unsigned int i = 0; i < N; ++i)
          REQUIRE(out.getPtr()[i] == reference[i]);
      }
    }
  }

  TEST_CASE("sampleCounter: processing allocates nothing (#1066)") {
    sampleCounter counter;
    buffer enable(N);
    enable = 1.f;
    counter();
    {
      TestHelpers::ProbeScope probe;
      counter.minimum(3).limit(50).start();
      counter();
      counter(32);
      counter(enable);
      counter.stop();
      counter();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
