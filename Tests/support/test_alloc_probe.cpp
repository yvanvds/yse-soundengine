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
//   - handed an allocation on *another* thread, the probe must stay at zero
//     too, and must still count the next one on its own thread (issue #701)
//
// A failure here does not mean an engine path regressed. It means the probe
// stopped seeing that shape of allocation, and every assertion in the suite
// that depends on it has quietly become vacuous — which is the state this
// file exists to make impossible to reach unnoticed.

#include <doctest/doctest.h>

#include <atomic>
#include <string>
#include <thread>
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

  // Thread scope (issue #701). The counter used to be a process-global atomic,
  // so an open probe counted every thread's allocations — the engine's slow
  // pool above all, whose worker allocates freely and legitimately (the
  // clockBridge resolve job builds a std::string to look a clock up by name)
  // and which several probed message handlers push work onto. That is a probe
  // that can fail for a reason unrelated to the path it asserts about.
  //
  // Both directions are checked here, because either alone is half a gate: a
  // probe that ignores other threads but has also stopped counting its own
  // would pass the first half and mean nothing.
  TEST_CASE("alloc probe: a probe counts only the thread that opened it (#701)") {
    if (!TestHelpers::probeCountsAllocations()) {
      MESSAGE("probe inert (ThreadSanitizer build) — nothing to observe");
      return;
    }

    std::atomic<bool> start{false};
    std::atomic<bool> finished{false};
    int workerCount = 0;

    // Started *before* the probe opens: constructing a std::thread allocates on
    // the creating thread, and that allocation is the test's own, not the
    // subject's. The handshake below is atomics and yields only — nothing on
    // this side of it may touch the heap.
    std::thread worker([&] {
      while (!start.load())
        std::this_thread::yield();
      {
        // The worker's own probe. It is what makes this test non-vacuous: it
        // proves the allocations below really happened (an elided allocation
        // is indistinguishable from one the probe missed) and that they were
        // counted somewhere — just not on the other thread's counter.
        TestHelpers::ProbeScope workerProbe;
        for (int i = 0; i < 64; ++i) {
          char* p = new char[g_size];
          g_ptr_sink = p;
          p[0] = 'y';
          g_sink = p[0];
          delete[] p;
          std::string s(g_size, 'y');
          g_ptr_sink = s.data();
          g_sink = s[0];
        }
        workerCount = TestHelpers::g_alloc_count.load();
      }
      finished.store(true);
    });

    // Read inside the scope, asserted outside it: doctest's own CHECK machinery
    // may allocate, which would land on this thread's counter and corrupt the
    // second reading.
    int duringOtherThread = -1;
    int afterOwnAllocation = -1;
    {
      TestHelpers::ProbeScope probe;
      start.store(true);
      while (!finished.load())
        std::this_thread::yield();
      duringOtherThread = TestHelpers::g_alloc_count.load();

      // Still armed, on this thread, after the worker opened *and closed* a
      // scope of its own: one array-new, counted exactly once. Without this the
      // zero above would also be produced by a probe that had been disarmed by
      // the other thread's ProbeScope destructor.
      char* p = new char[g_size];
      g_ptr_sink = p;
      p[0] = 'y';
      afterOwnAllocation = TestHelpers::g_alloc_count.load();
      g_sink = p[0];
      delete[] p;
    }

    worker.join();

    CHECK(duringOtherThread == 0);
    CHECK(afterOwnAllocation == 1);
    CHECK(workerCount > 0);
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
