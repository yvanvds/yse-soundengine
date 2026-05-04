// Tests for YSE::lfQueue — a lock-free SPSC circular queue (utils/lfQueue.hpp).

#include <doctest/doctest.h>
#include "utils/lfQueue.hpp"
#include <thread>
#include <vector>

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
    CHECK(q.try_push(4) == false);  // no allocation allowed by try_push
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

} // TEST_SUITE("utils")
