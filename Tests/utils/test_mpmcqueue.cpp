// Tests for YSE::mpmcQueue — a bounded lock-free MPMC queue (utils/mpmcQueue.hpp).
// Added with issue #188 (wait-free threadPool hand-off).

#include <doctest/doctest.h>
#include "utils/mpmcQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <set>

TEST_SUITE("utils") {

  TEST_CASE("mpmcQueue: single element push/pop round-trip") {
    YSE::mpmcQueue<int> q(16);
    CHECK(q.try_push(42) == true);
    int result = 0;
    CHECK(q.try_pop(result) == true);
    CHECK(result == 42);
  }

  TEST_CASE("mpmcQueue: try_pop on empty queue returns false") {
    YSE::mpmcQueue<int> q(16);
    int result = -1;
    CHECK(q.try_pop(result) == false);
    CHECK(result == -1); // left untouched
  }

  TEST_CASE("mpmcQueue: FIFO ordering with a single thread") {
    YSE::mpmcQueue<int> q(16);
    for (int i = 0; i < 8; ++i)
      CHECK(q.try_push(i));
    for (int i = 0; i < 8; ++i) {
      int v = -1;
      CHECK(q.try_pop(v));
      CHECK(v == i);
    }
  }

  TEST_CASE("mpmcQueue: capacity is rounded up to a power of two") {
    YSE::mpmcQueue<int> q(10);
    CHECK(q.capacity() == 16);
    YSE::mpmcQueue<int> q2(16);
    CHECK(q2.capacity() == 16);
    YSE::mpmcQueue<int> q3(1);
    CHECK(q3.capacity() == 2);
  }

  TEST_CASE("mpmcQueue: try_push returns false when full, no overwrite") {
    YSE::mpmcQueue<int> q(4); // holds exactly 4
    CHECK(q.capacity() == 4);
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    CHECK(q.try_push(4));
    CHECK(q.try_push(5) == false); // full — rejected
    // Draining still yields the first four in order.
    int v = 0;
    for (int expected = 1; expected <= 4; ++expected) {
      CHECK(q.try_pop(v));
      CHECK(v == expected);
    }
    CHECK(q.try_pop(v) == false);
  }

  TEST_CASE("mpmcQueue: wraps around the ring across many push/pop cycles") {
    YSE::mpmcQueue<int> q(4);
    for (int round = 0; round < 1000; ++round) {
      CHECK(q.try_push(round));
      int v = -1;
      CHECK(q.try_pop(v));
      CHECK(v == round);
    }
  }

  TEST_CASE("mpmcQueue: concurrent producers and consumers conserve every item") {
    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 4;
    constexpr int PER_PRODUCER = 20000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    YSE::mpmcQueue<int> q(1024);
    std::atomic<int> produced{0};
    std::atomic<long long> sumConsumed{0};
    std::atomic<int> consumedCount{0};
    std::atomic<bool> producersDone{false};

    std::vector<std::thread> threads;
    for (int p = 0; p < PRODUCERS; ++p) {
      threads.emplace_back([&, p] {
        for (int i = 0; i < PER_PRODUCER; ++i) {
          int value = p * PER_PRODUCER + i; // globally unique
          while (!q.try_push(value))
            std::this_thread::yield();
          produced.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    for (int c = 0; c < CONSUMERS; ++c) {
      threads.emplace_back([&] {
        int value = 0;
        for (;;) {
          if (q.try_pop(value)) {
            sumConsumed.fetch_add(value, std::memory_order_relaxed);
            consumedCount.fetch_add(1, std::memory_order_relaxed);
          } else if (producersDone.load(std::memory_order_acquire)) {
            // One last drain attempt after producers stop.
            if (!q.try_pop(value)) break;
            sumConsumed.fetch_add(value, std::memory_order_relaxed);
            consumedCount.fetch_add(1, std::memory_order_relaxed);
          } else {
            std::this_thread::yield();
          }
        }
      });
    }

    for (int p = 0; p < PRODUCERS; ++p)
      threads[p].join();
    producersDone.store(true, std::memory_order_release);
    for (int c = PRODUCERS; c < PRODUCERS + CONSUMERS; ++c)
      threads[c].join();

    CHECK(produced.load() == TOTAL);
    CHECK(consumedCount.load() == TOTAL);
    // Sum of all globally-unique values 0..TOTAL-1 — catches duplicates/drops.
    long long expectedSum = (long long)TOTAL * (TOTAL - 1) / 2;
    CHECK(sumConsumed.load() == expectedSum);
  }

} // TEST_SUITE
