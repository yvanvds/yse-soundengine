// Tests for YSE::lfQueue — a lock-free SPSC circular queue (utils/lfQueue.hpp).

#include <doctest/doctest.h>
#include "utils/lfQueue.hpp"
#include <thread>
#include <vector>

namespace {
  // Element type that tracks how many instances are currently alive. Used to
  // prove the queue destructor runs ~T() for every element it still holds.
  struct LiveCounter {
    static int live;
    int value;
    LiveCounter() : value(0) {
      ++live;
    }
    explicit LiveCounter(int v) : value(v) {
      ++live;
    }
    LiveCounter(const LiveCounter& o) : value(o.value) {
      ++live;
    }
    LiveCounter(LiveCounter&& o) noexcept : value(o.value) {
      ++live;
    }
    // Assignment transfers the value only — neither object is created or
    // destroyed, so the live count is unchanged.
    LiveCounter& operator=(const LiveCounter& o) {
      if (this != &o) value = o.value;
      return *this;
    }
    LiveCounter& operator=(LiveCounter&& o) noexcept {
      if (this != &o) value = o.value;
      return *this;
    }
    ~LiveCounter() {
      --live;
    }
  };
  int LiveCounter::live = 0;
} // namespace

TEST_SUITE("utils") {

  TEST_CASE("lfQueue: single element push/pop round-trip") {
    YSE::lfQueue<int> q(16);
    q.push(42);
    int result = 0;
    bool ok = q.try_pop(result);
    CHECK(ok == true);
    CHECK(result == 42);
  }

  TEST_CASE("lfQueue: try_pop on empty queue returns false") {
    YSE::lfQueue<int> q(16);
    int result = 0;
    CHECK(q.try_pop(result) == false);
  }

  TEST_CASE("lfQueue: FIFO ordering is preserved") {
    YSE::lfQueue<int> q(16);
    q.push(1);
    q.push(2);
    q.push(3);
    int a = 0, b = 0, c = 0;
    q.try_pop(a);
    q.try_pop(b);
    q.try_pop(c);
    CHECK(a == 1);
    CHECK(b == 2);
    CHECK(c == 3);
  }

  TEST_CASE("lfQueue: try_push returns false when the block is full") {
    // With maxSize=3 the internal block has 4 slots, wasting one for the ring
    // buffer sentinel — so exactly 3 elements fit before try_push returns false.
    YSE::lfQueue<int> q(3);
    CHECK(q.try_push(1) == true);
    CHECK(q.try_push(2) == true);
    CHECK(q.try_push(3) == true);
    CHECK(q.try_push(4) == false); // no allocation allowed by try_push
  }

  TEST_CASE("lfQueue: push() allocates and accepts beyond initial capacity") {
    YSE::lfQueue<int> q(3);
    // push() (as opposed to try_push()) is allowed to allocate new blocks
    for (int i = 0; i < 10; ++i)
      q.push(i);
    for (int i = 0; i < 10; ++i) {
      int val = 0;
      CHECK(q.try_pop(val) == true);
      CHECK(val == i);
    }
  }

  TEST_CASE("lfQueue: peek returns pointer to front element without consuming it") {
    YSE::lfQueue<int> q(8);
    q.push(99);
    int* p = q.peek();
    REQUIRE(p != nullptr);
    CHECK(*p == 99);
    int result = 0;
    CHECK(q.try_pop(result) == true);
    CHECK(result == 99);
  }

  TEST_CASE("lfQueue: peek on empty queue returns nullptr") {
    YSE::lfQueue<int> q(8);
    CHECK(q.peek() == nullptr);
  }

  TEST_CASE("lfQueue: SPSC producer/consumer all values received in order") {
    constexpr int N = 10000;
    YSE::lfQueue<int> q(N);
    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&]() {
      for (int i = 0; i < N; ++i)
        q.push(i);
    });

    std::thread consumer([&]() {
      int count = 0;
      while (count < N) {
        int val = 0;
        if (q.try_pop(val)) {
          received.push_back(val);
          ++count;
        }
      }
    });

    producer.join();
    consumer.join();

    REQUIRE(static_cast<int>(received.size()) == N);
    for (int i = 0; i < N; ++i)
      CHECK(received[i] == i);
  }

  TEST_CASE("lfQueue: destructor destroys every element across all blocks (issue #201)") {
    // Regression: the destructor walked frontBlock..tailBlock and stopped
    // *before* the tail block, so any live elements still sitting in the tail
    // block (and the tail block's memory, plus any free blocks) were leaked.
    LiveCounter::live = 0;
    {
      // maxSize=1 gives a 1-slot initial block; push() then doubles the block
      // size on each overflow, so 16 un-popped elements span several blocks and
      // the last few land in the tail block.
      YSE::lfQueue<LiveCounter> q(1);
      for (int i = 0; i < 16; ++i)
        q.push(LiveCounter(i));
      CHECK(LiveCounter::live == 16);
    } // queue destroyed here
    CHECK(LiveCounter::live == 0);
  }

  TEST_CASE("lfQueue: destructor walks the full block circle when free blocks exist "
            "(issue #201)") {
    // After allocating several blocks and then draining the queue, the block
    // circle contains empty "free" blocks between the tail and front. The
    // destructor must delete those too. This also exercises the full-circle
    // walk over empty blocks without double-free or crash.
    LiveCounter::live = 0;
    {
      YSE::lfQueue<LiveCounter> q(1);
      for (int i = 0; i < 16; ++i)
        q.push(LiveCounter(i));
      LiveCounter out;
      for (int i = 0; i < 16; ++i)
        CHECK(q.try_pop(out) == true);
      CHECK(LiveCounter::live == 1); // only `out` remains alive
      // Re-fill partially so the tail advances into the previously-freed blocks.
      for (int i = 0; i < 5; ++i)
        q.push(LiveCounter(i));
      CHECK(LiveCounter::live == 6); // `out` + 5 queued
    } // queue destroyed here — 5 queued elements must be destructed
    CHECK(LiveCounter::live == 0);
  }

} // TEST_SUITE("utils")
