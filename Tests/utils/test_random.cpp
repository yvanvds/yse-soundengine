// Tests for the pseudo-random helpers in YseEngine/utils/misc.hpp (issue #410).
//
// The helpers used to wrap C rand()/srand(); they now run a thread_local
// xorshift128+ generator with a Lemire multiply-shift reduction. This suite
// pins the contract that replacement has to keep:
//
//   - every helper stays inside its documented half-open range;
//   - degenerate inputs (empty or inverted ranges) return deterministically
//     instead of dividing by zero — `rand() % max` used to crash on max == 0,
//     which the patcher's gRandom object can reach from the audio thread;
//   - RandomF() carries more than the 15 bits RAND_MAX allowed on MSVC;
//   - concurrent threads draw from independent state, stay in range, and the
//     draw itself never allocates (audio-callback path, CLAUDE.md rule 3).

#include <doctest/doctest.h>

#include <cstddef>
#include <limits>
#include <set>
#include <thread>
#include <vector>

#include "support/alloc_probe.hpp"
#include "utils/misc.hpp"

TEST_SUITE("utils") {

  // Enough draws to make a missed bound overwhelmingly likely to show up,
  // cheap enough to stay in the fast unit suite.
  static const int kDraws = 20000;

  // --- Random(Int) ---

  TEST_CASE("Random(Int): stays inside [0, max)") {
    const Int bounds[] = {1, 2, 3, 50, 1000, 65537};
    for (Int max : bounds) {
      for (int i = 0; i < kDraws; ++i) {
        const Int v = YSE::Random(max);
        REQUIRE(v >= 0);
        REQUIRE(v < max);
      }
    }
  }

  TEST_CASE("Random(Int): reaches every value in the range") {
    std::set<Int> seen;
    for (int i = 0; i < kDraws; ++i)
      seen.insert(YSE::Random(8));
    CHECK(seen.size() == 8);
  }

  TEST_CASE("Random(Int): degenerate bounds return 0 instead of dividing by zero") {
    // max == 0 was the cpp:S3518 divide-by-zero in `rand() % max`.
    CHECK(YSE::Random(0) == 0);
    CHECK(YSE::Random(-1) == 0);
    CHECK(YSE::Random(-1000) == 0);
    CHECK(YSE::Random(std::numeric_limits<Int>::min()) == 0);
    // max == 1 has exactly one legal answer.
    for (int i = 0; i < 100; ++i)
      REQUIRE(YSE::Random(1) == 0);
  }

  // --- Random(Int, Int) ---

  TEST_CASE("Random(Int, Int): stays inside [min, max)") {
    struct Range {
      Int min;
      Int max;
    };
    const Range ranges[] = {{0, 10}, {-10, 10}, {-100, -50}, {5, 6}, {1000, 100000}};
    for (const Range& r : ranges) {
      for (int i = 0; i < kDraws; ++i) {
        const Int v = YSE::Random(r.min, r.max);
        REQUIRE(v >= r.min);
        REQUIRE(v < r.max);
      }
    }
  }

  TEST_CASE("Random(Int, Int): reaches every value in the range") {
    std::set<Int> seen;
    for (int i = 0; i < kDraws; ++i)
      seen.insert(YSE::Random(-2, 3));
    CHECK(seen.size() == 5);
    CHECK(*seen.begin() == -2);
    CHECK(*seen.rbegin() == 2);
  }

  TEST_CASE("Random(Int, Int): empty and inverted ranges return min") {
    CHECK(YSE::Random(7, 7) == 7);
    CHECK(YSE::Random(-5, -5) == -5);
    CHECK(YSE::Random(10, 3) == 10);
    CHECK(YSE::Random(0, -1) == 0);
  }

  TEST_CASE("Random(Int, Int): full-width span does not overflow") {
    // max - min is 2^32 - 1 here, which overflows Int; the helper widens to 64
    // bit before reducing.
    const Int lo = std::numeric_limits<Int>::min();
    const Int hi = std::numeric_limits<Int>::max();
    for (int i = 0; i < 2000; ++i) {
      const Int v = YSE::Random(lo, hi);
      REQUIRE(v >= lo);
      REQUIRE(v < hi);
    }
  }

  // --- BigRandom ---

  TEST_CASE("BigRandom: stays inside [0, max) and leans toward 0") {
    const Int max = 10000;
    double sum = 0.0;
    for (int i = 0; i < kDraws; ++i) {
      const Int v = YSE::BigRandom(max);
      REQUIRE(v >= 0);
      REQUIRE(v < max);
      sum += static_cast<double>(v);
    }
    // Product of two uniform [0, 100) draws: mean ~2450, well under the 5000 a
    // uniform draw over [0, max) would give.
    CHECK(sum / kDraws < max / 3.0);
  }

  TEST_CASE("BigRandom: degenerate and tiny bounds return 0") {
    // sqrt(max) was 0 for max == 0, so the old code divided by zero here too.
    CHECK(YSE::BigRandom(0) == 0);
    CHECK(YSE::BigRandom(-5) == 0);
    CHECK(YSE::BigRandom(std::numeric_limits<Int>::min()) == 0);
    // sqrt() floors to 1 for max in [1, 3], leaving a single legal answer.
    for (int i = 0; i < 100; ++i) {
      REQUIRE(YSE::BigRandom(1) == 0);
      REQUIRE(YSE::BigRandom(2) == 0);
      REQUIRE(YSE::BigRandom(3) == 0);
    }
  }

  // --- RandomF ---

  TEST_CASE("RandomF(): stays inside [0, 1) and spans it") {
    Flt lo = 1.0f;
    Flt hi = 0.0f;
    for (int i = 0; i < kDraws; ++i) {
      const Flt v = YSE::RandomF();
      REQUIRE(v >= 0.0f);
      REQUIRE(v < 1.0f);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    CHECK(lo < 0.01f);
    CHECK(hi > 0.99f);
  }

  TEST_CASE("RandomF(): resolution beats the 15-bit RAND_MAX of MSVC") {
    // rand()/RAND_MAX could only ever produce 32768 distinct values on MSVC, so
    // 100k draws could not exceed that count. The 24-bit replacement should
    // come close to 100k distinct values.
    std::set<Flt> seen;
    for (int i = 0; i < 100000; ++i)
      seen.insert(YSE::RandomF());
    CHECK(seen.size() > 50000);
  }

  TEST_CASE("RandomF(Flt): stays inside [0, max)") {
    for (int i = 0; i < kDraws; ++i) {
      const Flt v = YSE::RandomF(4.5f);
      REQUIRE(v >= 0.0f);
      REQUIRE(v < 4.5f);
    }
    for (int i = 0; i < 100; ++i)
      REQUIRE(YSE::RandomF(0.0f) == 0.0f);
    // A negative bound mirrors the interval: (max, 0].
    for (int i = 0; i < kDraws; ++i) {
      const Flt v = YSE::RandomF(-2.0f);
      REQUIRE(v <= 0.0f);
      REQUIRE(v > -2.0f);
    }
  }

  TEST_CASE("RandomF(Flt, Flt): stays inside [min, max)") {
    for (int i = 0; i < kDraws; ++i) {
      const Flt v = YSE::RandomF(-3.0f, 7.0f);
      REQUIRE(v >= -3.0f);
      REQUIRE(v < 7.0f);
    }
    for (int i = 0; i < 100; ++i)
      REQUIRE(YSE::RandomF(2.5f, 2.5f) == 2.5f);
    // An inverted range stays inside the closed hull rather than escaping it.
    for (int i = 0; i < kDraws; ++i) {
      const Flt v = YSE::RandomF(5.0f, 1.0f);
      REQUIRE(v >= 1.0f);
      REQUIRE(v <= 5.0f);
    }
  }

  // --- Random(Flt*, Flt*) ---

  TEST_CASE("Random(Flt*, Flt*): returns an in-range pointer on the Flt stride") {
    Flt block[8] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    Flt* begin = block;
    Flt* end = block + 8;

    std::set<std::ptrdiff_t> seen;
    for (int i = 0; i < kDraws; ++i) {
      Flt* p = YSE::Random(begin, end);
      REQUIRE(p >= begin);
      REQUIRE(p < end);
      const std::ptrdiff_t index = p - begin;
      // Whole-element stride: dereferencing must land on a real slot.
      REQUIRE(*p == doctest::Approx(static_cast<Flt>(index)));
      seen.insert(index);
    }
    CHECK(seen.size() == 8);
  }

  TEST_CASE("Random(Flt*, Flt*): empty and inverted ranges return min") {
    Flt block[8] = {};
    CHECK(YSE::Random(block, block) == block);
    CHECK(YSE::Random(block + 3, block + 3) == block + 3);
    CHECK(YSE::Random(block + 5, block + 2) == block + 5);
  }

  // --- seeding ---

  TEST_CASE("Randomize(): re-seeds without breaking the range contract") {
    YSE::Randomize();
    std::set<Int> seen;
    for (int i = 0; i < kDraws; ++i) {
      const Int v = YSE::Random(16);
      REQUIRE(v >= 0);
      REQUIRE(v < 16);
      seen.insert(v);
    }
    CHECK(seen.size() == 16);
  }

  // --- threading / real-time properties ---

  TEST_CASE("Random: concurrent threads stay in range and draw independent streams") {
    const int kThreads = 4;
    const int kPerThread = 5000;
    std::vector<std::vector<Int>> sequences(kThreads);
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      sequences[t].reserve(kPerThread);
      workers.emplace_back([&sequences, t, kPerThread]() {
        for (int i = 0; i < kPerThread; ++i)
          sequences[t].push_back(YSE::Random(1000000));
      });
    }
    for (std::thread& w : workers)
      w.join();

    for (int t = 0; t < kThreads; ++t) {
      CHECK(sequences[t].size() == static_cast<std::size_t>(kPerThread));
      for (Int v : sequences[t]) {
        REQUIRE(v >= 0);
        REQUIRE(v < 1000000);
      }
    }

    // Each thread seeds its own state from a distinct stream index, so no two
    // threads may replay the same sequence.
    for (int a = 0; a < kThreads; ++a)
      for (int b = a + 1; b < kThreads; ++b)
        CHECK(sequences[a] != sequences[b]);
  }

  TEST_CASE("Random: drawing never allocates") {
    Flt block[8] = {};
    // Warm the thread_local state up first: the one-time seed is lock- and
    // allocation-free too, but keeping it outside the probe keeps the assertion
    // about the steady-state draw.
    YSE::Random(10);

    volatile Int sinkI = 0;
    volatile Flt sinkF = 0.0f;
    volatile std::ptrdiff_t sinkP = 0;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 1000; ++i) {
        sinkI = YSE::Random(97);
        sinkI = YSE::Random(-5, 5);
        sinkI = YSE::BigRandom(400);
        sinkF = YSE::RandomF();
        sinkF = YSE::RandomF(3.0f);
        sinkF = YSE::RandomF(-1.0f, 1.0f);
        sinkP = YSE::Random(block, block + 8) - block;
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    (void)sinkI;
    (void)sinkF;
    (void)sinkP;
  }

} // TEST_SUITE("utils")
