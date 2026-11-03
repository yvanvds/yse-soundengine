// Gate on the TSan runtime's destroy-of-a-locked-mutex path (issue #1263).
//
// On compiler-rt 22 that path can leak a ScopedReport (see the comment on
// __tsan_default_options in Tests/main.cpp). The thread that destroyed the mutex
// then keeps ignore_interceptors raised and report_mtx locked. Its atomics stop
// synchronising, so the next correct hand-off from it reads as a race, and the
// report of that race blocks on report_mtx forever. That is the
// mpmcQueue<threadPoolJob*>::try_pop hang #1263 was filed for. PipeWire's libjack
// reaches the path from Pa_Initialize(), on some runs only, which made it look
// like an intermittent race in the queue.
//
// This case takes the same path on purpose. With report_destroy_locked=0, which
// is what yse_tests sets, nothing happens. With the check on, the hand-off below
// hangs until the ctest timeout. To see that, run this suite with
// TSAN_OPTIONS=report_destroy_locked=1. If the runtime can still restore the
// locker's stack, which a fixed compiler-rt may, TSan prints the destroy report
// and exits 66 instead.

#include <doctest/doctest.h>

// Same detection as support/alloc_probe.cpp.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define YSE_UNDER_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define YSE_UNDER_TSAN 1
#endif

#if defined(YSE_UNDER_TSAN)

#include <pthread.h>

#include <cstddef>
#include <thread>
#include <vector>

#include "utils/mpmcQueue.hpp"

namespace {
  // A trace part holds about 32000 events, at one event per write, and a thread
  // keeps three before it takes one from the recycle queue. This is well past
  // what it takes to reach the locker's part.
  constexpr std::size_t kTraceTraffic = 500000;
} // namespace

TEST_SUITE("tsanruntime") {

  TEST_CASE("a mutex destroyed while locked leaves this thread's hand-offs synchronised") {
    // Locked by a thread that exits still holding it, and destroyed by another,
    // which is the order libspa-support's loop teardown uses.
    pthread_mutex_t mutex;
    REQUIRE(pthread_mutex_init(&mutex, nullptr) == 0);
    std::thread locker([&mutex] { pthread_mutex_lock(&mutex); });
    locker.join();

    // A finished thread's trace parts are the first to be recycled, and the leak
    // needs the lock event gone from the history. Each write goes to a new
    // address because TSan doesn't trace a repeat of an access it just recorded.
    std::vector<long> traffic(kTraceTraffic);
    volatile long* word = traffic.data();
    for (std::size_t i = 0; i < kTraceTraffic; ++i)
      word[i] = 1;

    pthread_mutex_destroy(&mutex);

    // The exact pair the hang was reported on: this thread pushes, a fresh worker
    // pops. Correct code, so it stays silent unless this thread's release store
    // was dropped.
    YSE::mpmcQueue<int> queue{2};
    int popped = 0;
    std::thread worker([&queue, &popped] {
      while (!queue.try_pop(popped))
        std::this_thread::yield();
    });
    REQUIRE(queue.try_push(42));
    worker.join();
    CHECK(popped == 42);
  }
}

#endif // YSE_UNDER_TSAN
