// Self-test for the suite's heap-allocation probe (support/alloc_probe.hpp).
//
// Every other alloc-probe test in this suite asserts `g_alloc_count == 0` and
// concludes that an audio-thread path allocates nothing. That conclusion is
// only worth as much as the probe's reach, and the probe's reach is not
// obvious: it comes from replacing the global operator new, which counts an
// allocation only when the allocating code binds to the replacement.
//
// It did not, for std::string, on a MinGW build with a dynamically linked
// libc++ — the explicitly instantiated basic_string<char> allocates inside
// libc++.dll, and PE/COFF does not interpose across images. A 49-character
// concatenation counted 0 while a vector resize counted 1, so any zero-alloc
// assertion whose real target was a string build passed no matter what the
// code did (issue #697, found while fixing #690).
//
// This file is the gate. It does not test engine code; it tests the
// instrument, in both directions:
//
//   - handed code that certainly allocates, the probe must count it — one
//     case per allocation shape, including the std::string one that used to
//     be invisible and the array-new one that had no replaced operator at all
//   - handed code that certainly does not, the probe must stay at zero, so
//     the counter is not simply stuck high
//
// A failure here does not mean an engine path regressed. It means the probe
// stopped seeing that shape of allocation, and every assertion in the suite
// that depends on it has quietly become vacuous — which is the state this
// file exists to make impossible to reach unnoticed.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/alloc_probe.hpp"

// Same detection as alloc_probe.cpp, which compiles the replacements out under
// ThreadSanitizer because TSan ships its own operators.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define YSE_UNDER_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define YSE_UNDER_TSAN 1
#endif

namespace {
  // Volatile so nothing below can be constant-folded or elided: an allocation
  // the optimiser removed is indistinguishable from one the probe missed, and
  // the whole point here is to tell those apart. [expr.new]/12 lets a compiler
  // drop a new/delete pair outright — measured here, clang does exactly that
  // to the scalar and array cases from -O1 up — so every allocation escapes
  // into g_ptr_sink, which makes the storage observably used and the pair
  // unremovable at every level.
  volatile std::size_t g_size = 4096;
  volatile char g_sink = 0;
  volatile int g_int_sink = 0;
  void* volatile g_ptr_sink = nullptr;
} // namespace

TEST_SUITE("probe") {

  // Under ThreadSanitizer the replacements are compiled out (TSan ships its
  // own), so the counter never moves and the whole suite's zero-alloc
  // assertions hold trivially. That carve-out is documented in
  // alloc_probe.cpp; here it means the positive controls have nothing to
  // observe, so they are skipped rather than failed.
  TEST_CASE("alloc probe: the counter responds to a plain allocation") {
    const bool active = TestHelpers::probeCountsAllocations();
#ifdef YSE_UNDER_TSAN
    CHECK_FALSE(active);
#else
    CHECK(active);
#endif
  }

  TEST_CASE("alloc probe: every allocation shape under test is counted") {
    if (!TestHelpers::probeCountsAllocations()) {
      MESSAGE("probe inert (ThreadSanitizer build) — nothing to observe");
      return;
    }

    SUBCASE("scalar new") {
      int* p = nullptr;
      {
        TestHelpers::ProbeScope probe;
        p = new int(7);
        g_ptr_sink = p;
        CHECK(TestHelpers::g_alloc_count.load() == 1);
      }
      g_int_sink = *p;
      delete p;
    }

    // Nobody had replaced operator new[], so on PE/COFF this counted 0 too.
    SUBCASE("array new") {
      char* p = nullptr;
      {
        TestHelpers::ProbeScope probe;
        p = new char[g_size];
        g_ptr_sink = p;
        p[0] = 'y';
        CHECK(TestHelpers::g_alloc_count.load() == 1);
      }
      g_sink = p[0];
      delete[] p;
    }

    SUBCASE("vector growth") {
      TestHelpers::ProbeScope probe;
      std::vector<int> v;
      v.resize(g_size);
      v[0] = 7;
      g_ptr_sink = v.data();
      g_int_sink = v[0];
      CHECK(TestHelpers::g_alloc_count.load() > 0);
    }

    // The regression this file exists for. A std::string past the small-string
    // buffer allocates exactly like the vector above; if this reads 0 while
    // the vector case reads 1, the probe is blind to strings again and every
    // string-path assertion in the suite is unenforced.
    SUBCASE("std::string growth") {
      TestHelpers::ProbeScope probe;
      std::string s(g_size, 'y');
      g_ptr_sink = s.data();
      g_sink = s[0];
      CHECK(TestHelpers::g_alloc_count.load() > 0);
    }

    // The shape that motivated the issue: a log message being concatenated on
    // a path that claims to allocate nothing.
    SUBCASE("std::string concatenation") {
      TestHelpers::ProbeScope probe;
      std::string s =
          std::string("Cannot find target ") + std::string("nowhere") + ". Valid targets are xyz";
      g_ptr_sink = s.data();
      g_sink = s[0];
      CHECK(TestHelpers::g_alloc_count.load() > 0);
    }

    // The same claim as the SUBCASEs above, stated as the predicate the rest
    // of the suite can read, so a test author has one thing to check rather
    // than a platform table to reason about.
    SUBCASE("the capability predicate agrees") {
      CHECK(TestHelpers::probeSeesStringAllocations());
    }
  }

  // The other direction. Without this, a counter stuck permanently above zero
  // would pass every case above and fail every real test for the wrong reason.
  TEST_CASE("alloc probe: a genuinely allocation-free region counts zero") {
    if (!TestHelpers::probeCountsAllocations()) {
      MESSAGE("probe inert (ThreadSanitizer build) — nothing to observe");
      return;
    }

    // Pre-grown storage: the arithmetic below touches the heap only if
    // something other than this loop does.
    std::vector<int> v(64, 0);
    std::string s;
    s.reserve(g_size);

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 1000; ++i) {
        v[static_cast<std::size_t>(i) % v.size()] += i;
      }
      s.push_back('y');
      g_int_sink = v[0];
      g_sink = s[0];
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // The counter is armed by ProbeScope and disarmed by its destructor; a leak
  // in either direction would silently change what every other probe test
  // measures.
  TEST_CASE("alloc probe: the scope disarms on exit") {
    if (!TestHelpers::probeCountsAllocations()) {
      MESSAGE("probe inert (ThreadSanitizer build) — nothing to observe");
      return;
    }

    {
      TestHelpers::ProbeScope probe;
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // Outside the scope the counter must not move, whatever is allocated.
    const int before = TestHelpers::g_alloc_count.load();
    std::string s(g_size, 'y');
    g_sink = s[0];
    CHECK(TestHelpers::g_alloc_count.load() == before);
  }

} // TEST_SUITE("probe")
