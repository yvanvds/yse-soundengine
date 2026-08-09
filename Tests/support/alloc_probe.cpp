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
  namespace {
    // Probe state, per thread (issue #701). A replaced `operator new` is
    // process-wide and runs on every thread in the process, including ones that
    // allocate long before any probe exists; the *counting* is what has to be
    // scoped, so that a probe reports its own thread's allocations and not the
    // background pool's.
    //
    // Every one of these is a trivially destructible type with a constant
    // initializer on purpose. That is what makes the TLS access a plain slot
    // read: no lazy-init guard and no __cxa_thread_atexit registration, either
    // of which could allocate and re-enter the very operator that is reading
    // the slot.
    thread_local bool t_probe_active = false;
    thread_local int t_alloc_count = 0;

    // Single-block size watch — see the header (issue #662).
    thread_local bool t_watch_arm = false;
    thread_local void* t_watch_ptr = nullptr;
    thread_local std::size_t t_watch_new_size = 0;
    thread_local std::size_t t_watch_delete_size = 0;
    thread_local bool t_watch_sized_delete = false;
  } // namespace

  namespace detail {
    void probeArm() noexcept {
      t_alloc_count = 0;
      t_probe_active = true;
    }
    void probeDisarm() noexcept {
      t_probe_active = false;
    }
    int probeCount() noexcept {
      return t_alloc_count;
    }

    void watchArm() noexcept {
      t_watch_ptr = nullptr;
      t_watch_new_size = 0;
      t_watch_delete_size = 0;
      t_watch_sized_delete = false;
      t_watch_arm = true;
    }
    void watchDisarm() noexcept {
      t_watch_arm = false;
      t_watch_ptr = nullptr;
    }
    std::size_t watchNewSize() noexcept {
      return t_watch_new_size;
    }
    std::size_t watchDeleteSize() noexcept {
      return t_watch_delete_size;
    }
    bool watchSawSizedDelete() noexcept {
      return t_watch_sized_delete;
    }
  } // namespace detail
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
    // The whole cost the probe imposes on every allocation in the process: one
    // thread-local flag test. A thread with no probe open — the slow pool's
    // worker, PortAudio's callback thread — falls straight through.
    inline void count_new() noexcept {
      if (t_probe_active) ++t_alloc_count;
    }
    // Claim the first allocation made after a watch was armed, on the thread
    // that armed it.
    inline void watch_new(void* p, std::size_t n) noexcept {
      if (!p || !t_watch_arm) return;
      t_watch_arm = false;
      t_watch_new_size = n;
      t_watch_ptr = p;
    }
    inline void watch_delete(void* p, std::size_t n, bool sized) noexcept {
      if (!p || t_watch_ptr != p) return;
      t_watch_sized_delete = sized;
      t_watch_delete_size = n;
    }
  } // namespace
} // namespace TestHelpers

void* operator new(std::size_t n) {
  TestHelpers::count_new();
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
  TestHelpers::count_new();
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
  TestHelpers::count_new();
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc{};
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  TestHelpers::count_new();
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

  // Measured on whichever thread asks first and cached: the probe's reach is a
  // property of how the binary was linked, not of the calling thread. The
  // measurement itself allocates on the calling thread, inside its own scope,
  // so it is correct wherever it runs.
  bool probeCountsAllocations() {
    static const bool counted = [] {
      ProbeScope probe;
      std::vector<char> v;
      v.resize(g_canary_size);
      v[0] = 'y';
      g_canary_ptr = v.data();
      g_canary_sink = v[0];
      return g_alloc_count.load() > 0;
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
      return g_alloc_count.load() > 0;
    }();
    return counted;
  }

} // namespace TestHelpers
