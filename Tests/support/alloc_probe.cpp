// Single definition of the test-suite heap-allocation probe (see
// support/alloc_probe.hpp). The replaceable global operator new/delete live
// here — in exactly one TU — so including the header from many test files
// cannot produce a multiple-definition link error.

#include "support/alloc_probe.hpp"

#include <cstdlib>
#include <new>

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
#endif // YSE_UNDER_TSAN
