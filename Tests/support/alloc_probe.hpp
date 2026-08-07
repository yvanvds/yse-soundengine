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
#include <cstddef>

namespace TestHelpers {

  // Number of `operator new` calls observed while the probe is active.
  extern std::atomic<int> g_alloc_count;
  // When true, the global operator-new overrides increment g_alloc_count.
  extern std::atomic<bool> g_alloc_probe_active;

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
  extern std::atomic<bool> g_watch_arm; // capture the next operator new
  extern std::atomic<void*> g_watch_ptr; // the block that got captured
  extern std::atomic<std::size_t> g_watch_new_size; // size it was allocated with
  extern std::atomic<std::size_t> g_watch_delete_size; // size the sized delete got
  extern std::atomic<bool> g_watch_sized_delete; // the sized form was the one used

  // RAII arm: the next `operator new` after construction is the watched block.
  // Keep the scope alive across the matching delete, then compare newSize()
  // with deleteSize().
  struct AllocWatch {
    AllocWatch() {
      g_watch_ptr.store(nullptr, std::memory_order_relaxed);
      g_watch_new_size.store(0, std::memory_order_relaxed);
      g_watch_delete_size.store(0, std::memory_order_relaxed);
      g_watch_sized_delete.store(false, std::memory_order_relaxed);
      g_watch_arm.store(true, std::memory_order_relaxed);
    }
    ~AllocWatch() {
      g_watch_arm.store(false, std::memory_order_relaxed);
      g_watch_ptr.store(nullptr, std::memory_order_relaxed);
    }
    AllocWatch(const AllocWatch&) = delete;
    AllocWatch& operator=(const AllocWatch&) = delete;

    // 0 when nothing was captured — the overrides are compiled out under
    // ThreadSanitizer, which ships its own operators.
    std::size_t newSize() const {
      return g_watch_new_size.load(std::memory_order_relaxed);
    }
    std::size_t deleteSize() const {
      return g_watch_delete_size.load(std::memory_order_relaxed);
    }
    // False when the toolchain routed the free through the unsized
    // `operator delete(void*)`, which carries no size to compare.
    bool sawSizedDelete() const {
      return g_watch_sized_delete.load(std::memory_order_relaxed);
    }
  };

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
