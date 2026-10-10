#pragma once
// Shared heap-allocation probe for the test suite.
//
// The replaceable global operator new/delete are defined once in
// alloc_probe.cpp (a single TU — defining them in a header would be an ODR /
// multiple-definition violation the moment two TUs include it). While a probe
// is armed the overrides count `operator new` calls; the rest of the time they
// are transparent so doctest/STL/etc. are unaffected. Wrap the region under
// test in a `ProbeScope` and assert `g_alloc_count.load() == 0`.
//
// Two properties of the instrument decide whether such an assertion means
// anything, and both are measured rather than assumed:
//
//   * its *reach* — read `probeSeesStringAllocations()` below before asserting
//     over a path that can build a std::string; what the probe observes
//     depends on the object format and on how the C++ runtime is linked
//     (issue #697)
//   * its *scope* — a probe counts only the thread that opened it (issue
//     #701), so the code under test has to run on that thread
//
// Extracted from test_named_bus.cpp (issue #121) so the manager / virtualFinder
// RT-allocation tests (issue #194) can share the same probe.

#include <cstddef>

namespace TestHelpers {

  // Implementation hooks. The state they touch is `thread_local` and lives in
  // alloc_probe.cpp — the same TU as the replaced operators, which keeps the
  // TLS access local to one image and keeps `operator new` off any symbol
  // another image would have to resolve for it.
  namespace detail {
    // Zero this thread's counter and start counting on it.
    void probeArm() noexcept;
    // Stop counting on this thread. Leaves the count readable.
    void probeDisarm() noexcept;
    // This thread's count.
    int probeCount() noexcept;

    // Single-block watch, same thread scoping — see AllocWatch below.
    void watchArm() noexcept;
    void watchDisarm() noexcept;
    std::size_t watchNewSize() noexcept;
    std::size_t watchDeleteSize() noexcept;
    bool watchSawSizedDelete() noexcept;
  } // namespace detail

  // ── Thread scope (issue #701) ─────────────────────────────────────────────
  //
  // The counter used to be a process-global atomic, so while a scope was open
  // *any* thread's `operator new` incremented it. The engine keeps a background
  // "slow pool" whose worker allocates freely and legitimately — clockBridge's
  // resolve job builds a std::string to look a clock up by name, for one — and
  // several probes are open across a window in which a message handler has just
  // pushed a job onto that pool. Those probes could fail on work that has
  // nothing to do with the path they assert about: not a vacuous pass, a false
  // failure, and the kind that gets "fixed" by widening a sleep.
  //
  // So the count is per-thread and a `ProbeScope` arms only the thread that
  // constructs it. Every call site drives the code under test on the test
  // thread itself, so what each of them measures is unchanged — they simply
  // stop seeing other threads.
  //
  // The corollary is the rule for new call sites: **drive the probed code on
  // the thread that opened the scope.** A scope opened here cannot observe an
  // allocation made on the audio callback or on the pool; to measure one of
  // those, open a `ProbeScope` on *that* thread and hand its count back (see
  // the two-thread case in support/test_alloc_probe.cpp, which does exactly
  // that to prove the scoping works).

  // Reads the calling thread's allocation count. Spelled as an object with a
  // `load()` so every call site keeps the shape it had when this was an atomic,
  // while the storage behind it is thread_local.
  struct ThreadAllocCount {
    int load() const noexcept {
      return detail::probeCount();
    }
  };
  inline constexpr ThreadAllocCount g_alloc_count{};

  // ── Single-block size watch (issue #662) ──────────────────────────────────
  //
  // Records the byte count `operator new` served one specific block with, and
  // the byte count the *sized* `operator delete` was later handed back for that
  // same block. A mismatch is what AddressSanitizer calls a
  // new-delete-type-mismatch, and it is exactly what deleting a subclass
  // through a non-virtual base pointer produces: the compiler passes
  // sizeof(Base) for an allocation that is sizeof(Derived) bytes, and
  // allocators that trust the hint free into the wrong size class.
  //
  // Detectable here without a sanitizer, on every platform, because this suite
  // already replaces both operators — see alloc_probe.cpp. Nothing is recorded
  // unless a watch is armed, so the rest of the suite is unaffected.
  //
  // Thread-scoped for the same reason as the counter (issue #701): the watch
  // claims "the next allocation", and a process-wide watch would happily claim
  // one made by the pool worker instead of the one under test. Both halves of
  // the new/delete pair therefore have to run on the arming thread.
  struct AllocWatch {
    AllocWatch() {
      detail::watchArm();
    }
    ~AllocWatch() {
      detail::watchDisarm();
    }
    AllocWatch(const AllocWatch&) = delete;
    AllocWatch& operator=(const AllocWatch&) = delete;

    // 0 when nothing was captured. The overrides are compiled out under
    // ThreadSanitizer, which ships its own operators, and under the Windows
    // ASan build, where the runtime owns them for the same reason (issue #671)
    // — there the watch is inert because the sanitizer's free hook carries no
    // size, and ASan reports the mismatch itself as a hard error instead.
    std::size_t newSize() const {
      return detail::watchNewSize();
    }
    std::size_t deleteSize() const {
      return detail::watchDeleteSize();
    }
    // False when the toolchain routed the free through the unsized
    // `operator delete(void*)`, which carries no size to compare.
    bool sawSizedDelete() const {
      return detail::watchSawSizedDelete();
    }
  };

  // ── What the probe can actually see (issue #697) ──────────────────────────
  //
  // Replacing the global `operator new` only counts allocations made by code
  // that *binds* to the replacement, and that is a property of the object
  // format, not of the language.
  //
  // On ELF (Linux, Android) every shared object's call to `operator new` goes
  // through the PLT and is preempted by the executable's definition, so the
  // probe sees every allocation in the process — including the ones libstdc++
  // makes inside `std::basic_string<char>`.
  //
  // On PE/COFF (Windows) there is no cross-image interposition: a DLL's calls
  // to `operator new` resolve inside that DLL. libc++ explicitly instantiates
  // `std::basic_string<char>` and exports it from libc++.dll, so with a
  // dynamically linked C++ runtime every std::string allocation the engine
  // makes — a log message above all — happened past the replacement and
  // counted 0. A `ProbeScope` around such a path asserted `g_alloc_count == 0`
  // and passed whether or not the allocation happened. Tests/CMakeLists.txt
  // therefore links yse_tests against a *static* C++ runtime on MinGW, which
  // pulls those instantiations into the binary and back under the
  // replacement.
  //
  // The two predicates below measure the outcome instead of assuming it, and
  // support/test_alloc_probe.cpp asserts them. That is what keeps the fix from
  // rotting quietly: if a toolchain or link change ever puts string
  // allocations back out of reach, the suite fails there rather than turning
  // every string-path assertion vacuous without a word.
  //
  // Both are measured once, on first call, and cached. Neither may be called
  // from inside a `ProbeScope` — each opens its own and allocates on purpose.

  // True when the probe counts anything at all. False under ThreadSanitizer,
  // which ships its own operators and compiles ours out (see alloc_probe.cpp);
  // there every `g_alloc_count == 0` assertion holds trivially.
  bool probeCountsAllocations();

  // True when the probe counts `std::string`'s own heap allocations, and so
  // when a zero-allocation claim over a path that could build a string means
  // anything.
  bool probeSeesStringAllocations();

  // RAII activation: zeroes the counter and arms the probe for its lifetime —
  // on the constructing thread only.
  struct ProbeScope {
    ProbeScope() noexcept {
      detail::probeArm();
    }
    ~ProbeScope() noexcept {
      detail::probeDisarm();
    }
    // Non-copyable since the scoping is per-thread: a copy destroyed on
    // another thread would disarm that one and leave this one counting.
    ProbeScope(const ProbeScope&) = delete;
    ProbeScope& operator=(const ProbeScope&) = delete;
  };

} // namespace TestHelpers
