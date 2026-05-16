// Tests for YSE::PATCHER::timerThread (YseEngine/patcher/time/TimerThread.{cpp,h}).
//
// Drives the public timer API (Add/setInterval/setTimeout/ClearTimer/Clear/
// size/empty) end-to-end so the worker thread actually fires user callbacks.
// In particular this suite exercises the cancellation handshake added for
// cpp:S5404 (wait-with-predicate) by clearing a timer mid-callback — the
// destroyImpl thread parks on `timer.waitCond` and the worker signals
// `destroyed = true` before notify_all() so the predicate unblocks safely.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "patcher/time/TimerThread.h"

using namespace std::chrono_literals;

namespace {

// Spin-wait for `pred()` to become true, polling every 1ms up to `budget` ms.
// Returns true if the predicate fired in time, false on timeout. Tests that
// must observe an asynchronous callback use this rather than a hard sleep so
// they finish as quickly as the worker thread schedules.
template <typename P>
bool waitFor(P pred, int budgetMs = 1000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

} // namespace

TEST_SUITE("patcher") {

TEST_CASE("timerThread: setTimeout fires the callback once") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    t.setTimeout([&count]{ count++; }, 10);
    CHECK(waitFor([&]{ return count.load() >= 1; }));
    // give the worker a beat to confirm no second tick arrives
    std::this_thread::sleep_for(40ms);
    CHECK(count.load() == 1);
}

TEST_CASE("timerThread: setInterval fires periodically") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    auto id = t.setInterval([&count]{ count++; }, 10);
    CHECK(waitFor([&]{ return count.load() >= 3; }, 500));
    t.ClearTimer(id);
}

TEST_CASE("timerThread: Add returns increasing IDs and respects period") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> a{0}, b{0};
    auto id1 = t.Add(5, 0, [&a]{ a++; });
    auto id2 = t.Add(5, 0, [&b]{ b++; });
    CHECK(id1 != id2);
    CHECK(waitFor([&]{ return a.load() == 1 && b.load() == 1; }));
}

TEST_CASE("timerThread: ClearTimer returns false for unknown id") {
    YSE::PATCHER::timerThread t;
    CHECK_FALSE(t.ClearTimer(99999));
}

TEST_CASE("timerThread: ClearTimer succeeds before the timer fires") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    // long delay → cancel before the worker can fire it
    auto id = t.Add(5000, 0, [&count]{ count++; });
    CHECK(t.ClearTimer(id));
    std::this_thread::sleep_for(20ms);
    CHECK(count.load() == 0);
}

TEST_CASE("timerThread: ClearTimer during callback uses wait-predicate handshake") {
    YSE::PATCHER::timerThread t;
    std::atomic<bool> entered{false};
    std::atomic<bool> mayExit{false};
    std::atomic<bool> exited{false};

    auto id = t.setInterval([&]{
        entered = true;
        // hold inside the callback until the test thread allows it to return
        while (!mayExit.load()) std::this_thread::sleep_for(1ms);
        exited = true;
    }, 1);

    REQUIRE(waitFor([&]{ return entered.load(); }));

    // ClearTimer blocks on the predicate-checked wait until the callback exits
    std::thread canceller([&]{ t.ClearTimer(id); });

    // release the callback; the worker will signal destroyed=true + notify_all
    mayExit = true;
    canceller.join();

    CHECK(exited.load());
    CHECK(t.empty());
}

TEST_CASE("timerThread: Clear removes every pending timer") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    t.Add(5000, 0, [&]{ count++; });
    t.Add(5000, 0, [&]{ count++; });
    t.Add(5000, 0, [&]{ count++; });
    CHECK(t.size() == 3);
    CHECK_FALSE(t.empty());
    t.Clear();
    CHECK(t.size() == 0);
    CHECK(t.empty());
    std::this_thread::sleep_for(20ms);
    CHECK(count.load() == 0);
}

TEST_CASE("timerThread: size and empty reflect adds and clears") {
    YSE::PATCHER::timerThread t;
    CHECK(t.empty());
    CHECK(t.size() == 0);
    auto a = t.Add(5000, 0, []{});
    auto b = t.Add(5000, 0, []{});
    CHECK(t.size() == 2);
    CHECK_FALSE(t.empty());
    t.ClearTimer(a);
    CHECK(t.size() == 1);
    t.ClearTimer(b);
    CHECK(t.size() == 0);
    CHECK(t.empty());
}

TEST_CASE("timerThread: setInterval with chrono duration overloads") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    YSE::PATCHER::timerThread::boundHandlerType<> cb = [&count]{ count++; };
    auto id = t.Add(std::chrono::milliseconds(5),
                    std::chrono::milliseconds(10),
                    cb);
    CHECK(waitFor([&]{ return count.load() >= 2; }, 500));
    t.ClearTimer(id);
}

} // TEST_SUITE("patcher")
