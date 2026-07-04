#pragma once
// Shared heap-allocation probe for the test suite.
//
// The replaceable global operator new/delete are defined once in
// alloc_probe.cpp (a single TU — defining them in a header would be an ODR /
// multiple-definition violation the moment two TUs include it). While
// `g_alloc_probe_active` is true the overrides count every `operator new`
// call; the rest of the time they are transparent so doctest/STL/etc. are
// unaffected. Wrap the region under test in a `ProbeScope` and assert
// `g_alloc_count == 0`.
//
// Extracted from test_named_bus.cpp (issue #121) so the manager / virtualFinder
// RT-allocation tests (issue #194) can share the same probe.

#include <atomic>

namespace TestHelpers {

  // Number of `operator new` calls observed while the probe is active.
  extern std::atomic<int> g_alloc_count;
  // When true, the global operator-new overrides increment g_alloc_count.
  extern std::atomic<bool> g_alloc_probe_active;

  // RAII activation: zeroes the counter and arms the probe for its lifetime.
  struct ProbeScope {
    ProbeScope() {
      g_alloc_count.store(0, std::memory_order_relaxed);
      g_alloc_probe_active.store(true, std::memory_order_relaxed);
    }
    ~ProbeScope() {
      g_alloc_probe_active.store(false, std::memory_order_relaxed);
    }
  };

} // namespace TestHelpers
