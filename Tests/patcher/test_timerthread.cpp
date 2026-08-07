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
  template <typename P> bool waitFor(P pred, int budgetMs = 1000) {
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
    t.setTimeout([&count] { count++; }, 10);
    CHECK(waitFor([&] { return count.load() >= 1; }));
    // give the worker a beat to confirm no second tick arrives
    std::this_thread::sleep_for(40ms);
    CHECK(count.load() == 1);
  }

  TEST_CASE("timerThread: setInterval fires periodically") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    auto id = t.setInterval([&count] { count++; }, 10);
    CHECK(waitFor([&] { return count.load() >= 3; }, 500));
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: Add returns increasing IDs and respects period") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> a{0}, b{0};
    auto id1 = t.Add(5, 0, [&a] { a++; });
    auto id2 = t.Add(5, 0, [&b] { b++; });
    CHECK(id1 != id2);
    CHECK(waitFor([&] { return a.load() == 1 && b.load() == 1; }));
  }

  TEST_CASE("timerThread: ClearTimer returns false for unknown id") {
    YSE::PATCHER::timerThread t;
    CHECK_FALSE(t.ClearTimer(99999));
  }

  TEST_CASE("timerThread: ClearTimer succeeds before the timer fires") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    // long delay → cancel before the worker can fire it
    auto id = t.Add(5000, 0, [&count] { count++; });
    CHECK(t.ClearTimer(id));
    std::this_thread::sleep_for(20ms);
    CHECK(count.load() == 0);
  }

  TEST_CASE("timerThread: ClearTimer during callback uses wait-predicate handshake") {
    YSE::PATCHER::timerThread t;
    std::atomic<bool> entered{false};
    std::atomic<bool> mayExit{false};
    std::atomic<bool> exited{false};

    auto id = t.setInterval(
        [&] {
          entered = true;
          // hold inside the callback until the test thread allows it to return
          while (!mayExit.load())
            std::this_thread::sleep_for(1ms);
          exited = true;
        },
        1);

    REQUIRE(waitFor([&] { return entered.load(); }));

    // ClearTimer blocks on the predicate-checked wait until the callback exits
    std::thread canceller([&] { t.ClearTimer(id); });

    // release the callback; the worker will signal destroyed=true + notify_all
    mayExit = true;
    canceller.join();

    CHECK(exited.load());
    CHECK(t.empty());
  }

  TEST_CASE("timerThread: Clear removes every pending timer") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    t.Add(5000, 0, [&] { count++; });
    t.Add(5000, 0, [&] { count++; });
    t.Add(5000, 0, [&] { count++; });
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
    auto a = t.Add(5000, 0, [] {});
    auto b = t.Add(5000, 0, [] {});
    CHECK(t.size() == 2);
    CHECK_FALSE(t.empty());
    t.ClearTimer(a);
    CHECK(t.size() == 1);
    t.ClearTimer(b);
    CHECK(t.size() == 0);
    CHECK(t.empty());
  }

  TEST_CASE("timerThread: cancel while the worker parks in wait_until (regression #240)") {
    // The worker parks in wait_until(lock, timer.next), where `timer` is a
    // reference into the `active` map node. A concurrent ClearTimer erases and
    // frees that node while the wait has the lock released; wait_until then
    // re-reads timer.next after re-locking to compute its return status. Before
    // the fix that read touched freed memory (ASan heap-use-after-free at
    // TimerThread.cpp:51). Churn add→park→cancel many times to hit the window.
    YSE::PATCHER::timerThread t;
    for (int i = 0; i < 500; ++i) {
      // 200ms deadline so the worker enters wait_until rather than firing;
      // the callback must never run within this loop.
      auto id = t.Add(200, 0, [] {});
      // Give the worker a beat to pick up the timer and park in wait_until.
      std::this_thread::sleep_for(1ms);
      t.ClearTimer(id); // frees the node the parked wait_until references
    }
    CHECK(t.empty());
  }

  // ─── SetPeriod (issue #625) ───────────────────────────────────────────────
  //
  // A running timer used to be stuck on the interval it was scheduled with:
  // `Timer::period` is copied at Add time and nothing could rewrite it. The
  // rescheduling rule is "next expiry = previous expiry + new period", clamped
  // to now when the shrink has already overshot the current cycle.

  TEST_CASE("timerThread: SetPeriod rejects an unknown id and a non-positive period") {
    YSE::PATCHER::timerThread t;
    CHECK_FALSE(t.SetPeriod(99999, 10));

    auto id = t.setInterval([] {}, 5000);
    // A zero/negative period would demote the periodic timer to a one-shot
    // inside the worker and drop it from `active` behind the caller's back.
    CHECK_FALSE(t.SetPeriod(id, 0));
    CHECK_FALSE(t.SetPeriod(id, -5));
    CHECK(t.size() == 1);
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: SetPeriod speeds up a pending interval without restarting it") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    // 5s cycle: the timer is parked far in the future, so any tick observed
    // below can only come from the reschedule.
    auto id = t.setInterval([&count] { count++; }, 5000);
    std::this_thread::sleep_for(20ms);
    REQUIRE(count.load() == 0);

    // 20ms of the cycle has elapsed and the new period is 10ms, so the target
    // expiry is already in the past — the timer must fire promptly rather than
    // wait out a fresh 10ms, and must not skip the beat entirely.
    CHECK(t.SetPeriod(id, 10));
    CHECK(waitFor([&] { return count.load() >= 5; }, 1000));
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: SetPeriod slows a pending interval down") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    auto id = t.setInterval([&count] { count++; }, 10);
    REQUIRE(waitFor([&] { return count.load() >= 2; }, 1000));

    CHECK(t.SetPeriod(id, 5000));
    // Let any tick already in flight land, then the queue must go quiet.
    std::this_thread::sleep_for(30ms);
    const int settled = count.load();
    std::this_thread::sleep_for(150ms);
    CHECK(count.load() == settled);
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: SetPeriod from inside the callback applies to the next cycle") {
    // The `.metro` parameter path relies on this: the only thread that learns
    // about the new interval is the timer thread itself, mid-callback. The
    // timer is not queued at that point, so SetPeriod may only store the value
    // and let the worker's own `next += period` reschedule use it — and it must
    // not deadlock against the worker, which holds no lock across the callback.
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    std::atomic<YSE::PATCHER::timerThread::timerID> self{0};

    auto id = t.setInterval(
        [&] {
          count++;
          auto me = self.load();
          if (me != 0) t.SetPeriod(me, 10);
        },
        400);
    self.store(id);

    // First tick at ~400ms switches the timer to 10ms; without that taking
    // effect, five ticks would need two full seconds.
    CHECK(waitFor([&] { return count.load() >= 5; }, 1200));
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: SetPeriod touches only the timer it names") {
    // The queue is a multiset keyed on the deadline, so SetPeriod has to pull
    // its entry out by identity: the by-key erase overload would take every
    // entry with an equivalent deadline with it. Two timers on the same nominal
    // delay is the shape that case has (they rarely land on the same
    // nanosecond, so this pins the neighbour-untouched contract rather than the
    // collision itself).
    YSE::PATCHER::timerThread t;
    std::atomic<int> a{0}, b{0};
    auto idA = t.Add(5000, 5000, [&a] { a++; });
    auto idB = t.Add(5000, 5000, [&b] { b++; });
    REQUIRE(t.size() == 2);

    CHECK(t.SetPeriod(idA, 10));
    CHECK(t.size() == 2);
    CHECK(waitFor([&] { return a.load() >= 3; }, 1000));
    CHECK(b.load() == 0); // idB keeps its own 5s interval

    t.ClearTimer(idA);
    t.ClearTimer(idB);
  }

  TEST_CASE("timerThread: setInterval with chrono duration overloads") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    YSE::PATCHER::timerThread::boundHandlerType<> cb = [&count] { count++; };
    auto id = t.Add(std::chrono::milliseconds(5), std::chrono::milliseconds(10), cb);
    CHECK(waitFor([&] { return count.load() >= 2; }, 500));
    t.ClearTimer(id);
  }

} // TEST_SUITE("patcher")
