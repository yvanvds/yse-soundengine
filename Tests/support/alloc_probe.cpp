// Single definition of the test-suite heap-allocation probe (see
// support/alloc_probe.hpp). The replaceable global operator new/delete live
// here — in exactly one TU — so including the header from many test files
// cannot produce a multiple-definition link error.

#include "support/alloc_probe.hpp"

#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace TestHelpers {
  std::atomic<int> g_alloc_count{0};
  std::atomic<bool> g_alloc_probe_active{false};

  // Single-block size watch — see the header (issue #662).
  std::atomic<bool> g_watch_arm{false};
  std::atomic<void*> g_watch_ptr{nullptr};
  std::atomic<std::size_t> g_watch_new_size{0};
  std::atomic<std::size_t> g_watch_delete_size{0};
  std::atomic<bool> g_watch_sized_delete{false};
} // namespace TestHelpers

// ThreadSanitizer ships its own replaceable operator new/delete in
// libclang_rt.tsan_cxx, so defining ours too is a multiple-definition link
// error (issue #229 wired a TSan build of the test binary). Skip the probe
// under TSan: the audio-path checks assert g_alloc_count == 0, which then holds
// trivially because the counter is never touched. ASan tolerates the override,
// so it is kept there.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define YSE_UNDER_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define YSE_UNDER_TSAN 1
#endif

#ifndef YSE_UNDER_TSAN
namespace TestHelpers {
  namespace {
    // Claim the first allocation made after a watch was armed.
    inline void watch_new(void* p, std::size_t n) {
      if (!p) return;
      if (!g_watch_arm.exchange(false, std::memory_order_relaxed)) return;
      g_watch_new_size.store(n, std::memory_order_relaxed);
      g_watch_ptr.store(p, std::memory_order_relaxed);
    }
    inline void watch_delete(void* p, std::size_t n, bool sized) {
      if (!p || g_watch_ptr.load(std::memory_order_relaxed) != p) return;
      g_watch_sized_delete.store(sized, std::memory_order_relaxed);
      g_watch_delete_size.store(n, std::memory_order_relaxed);
    }
  } // namespace
} // namespace TestHelpers

void* operator new(std::size_t n) {
  if (TestHelpers::g_alloc_probe_active.load(std::memory_order_relaxed))
    TestHelpers::g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) {
    TestHelpers::watch_new(p, n);
    return p;
  }
  throw std::bad_alloc{};
}

// Route the nothrow form through malloc too. libsndfile's sndfile.hh allocates
// SNDFILE_ref with `new (std::nothrow)`; without this override that allocation
// would go through the default (ASan-instrumented) operator new while the
// matching delete below frees it with std::free, which AddressSanitizer flags
// as an alloc-dealloc-mismatch (issue #219).
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (TestHelpers::g_alloc_probe_active.load(std::memory_order_relaxed))
    TestHelpers::g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  void* p = std::malloc(n == 0 ? 1 : n);
  TestHelpers::watch_new(p, n);
  return p;
}

// The array forms (issue #697). They are separately replaceable, and leaving
// them out was a second blind spot: on PE/COFF the default `operator new[]`
// lives in the C++ runtime, so `new T[n]` from a test or from engine code
// counted 0. Each one counts and allocates on its own rather than delegating
// to the scalar form above — on ELF that delegation is preempted too, so
// forwarding would count one `new T[n]` twice.
//
// Deliberately not wired into the AllocWatch: that watch exists to catch a
// sized-delete mismatch from deleting a subclass through a non-virtual base
// pointer, which is a scalar-delete story. Claiming an array allocation as
// "the next operator new" would only let unrelated traffic steal the watch.
void* operator new[](std::size_t n) {
  if (TestHelpers::g_alloc_probe_active.load(std::memory_order_relaxed))
    TestHelpers::g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc{};
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  if (TestHelpers::g_alloc_probe_active.load(std::memory_order_relaxed))
    TestHelpers::g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n == 0 ? 1 : n);
}

void operator delete(void* p) noexcept {
  TestHelpers::watch_delete(p, 0, false);
  std::free(p);
}
void operator delete(void* p, std::size_t n) noexcept {
  TestHelpers::watch_delete(p, n, true);
  std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
  TestHelpers::watch_delete(p, 0, false);
  std::free(p);
}
// Matching array deletes: every block above came from std::malloc, so it has
// to come back to std::free. Without these the runtime's own operator delete[]
// would free it, which AddressSanitizer reports as an alloc-dealloc-mismatch
// (the same reason the nothrow scalar form is replaced — issue #219).
void operator delete[](void* p) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}
#endif // YSE_UNDER_TSAN

// ── Capability measurement (issue #697) ─────────────────────────────────────
//
// Deliberately measured rather than deduced from #ifdefs: the answer depends
// on the object format *and* on how this binary happened to be linked, and a
// probe that guesses wrong about its own reach is exactly the failure the
// issue describes. See support/test_alloc_probe.cpp for the gate that reads
// these.
namespace {
  // Kept volatile so neither the canary allocation nor its size can be
  // constant-folded away: an elided allocation would look like a blind probe,
  // and [expr.new]/12 lets a compiler drop one. Escaping the buffer's address
  // into g_canary_ptr makes the storage observably used, which is what keeps
  // the allocation.
  volatile std::size_t g_canary_size = 4096;
  volatile char g_canary_sink = 0;
  void* volatile g_canary_ptr = nullptr;
} // namespace

namespace TestHelpers {

  bool probeCountsAllocations() {
    static const bool counted = [] {
      ProbeScope probe;
      std::vector<char> v;
      v.resize(g_canary_size);
      v[0] = 'y';
      g_canary_ptr = v.data();
      g_canary_sink = v[0];
      return g_alloc_count.load(std::memory_order_relaxed) > 0;
    }();
    return counted;
  }

  bool probeSeesStringAllocations() {
    static const bool counted = [] {
      ProbeScope probe;
      // Well past any small-string buffer, so the heap is the only place this
      // can live.
      std::string s(g_canary_size, 'y');
      g_canary_ptr = s.data();
      g_canary_sink = s[0];
      return g_alloc_count.load(std::memory_order_relaxed) > 0;
    }();
    return counted;
  }

} // namespace TestHelpers
