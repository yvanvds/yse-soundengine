// Tests for YSE::PATCHER::timerThread (YseEngine/patcher/time/TimerThread.{cpp,h}).
//
// Drives the public timer API (Add/setInterval/setTimeout/ClearTimer/Clear/
// size/empty) end-to-end so the worker thread actually fires user callbacks.
// In particular this suite exercises the cancellation handshake: clearing a
// timer mid-callback from another thread parks the caller on the object's
// `retired` condition variable until the worker has finished that callback and
// dropped the timer from `active`, the predicate-checked wait guarding against
// spurious wakeup (cpp:S5404). Since #722 the same call made from *inside* the
// callback records the retirement and returns instead, which is the half that
// used to park the one worker in the process forever (#721).

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include "patcher/time/TimerThread.h"
#include "support/timer_pacing.hpp"

using namespace std::chrono_literals;

namespace {

  // Spin-wait for `pred()` to become true, budgeted in *ticks of the suite's
  // reference timer* rather than in milliseconds (issue #753). The worker gets
  // that many millisecond deliveries to satisfy the predicate, however long a
  // loaded box takes to produce them, so a case here fails when this
  // `timerThread` misbehaves and not when the scheduler is busy.
  template <typename P> bool waitTicks(P pred, int ticks = 1000) {
    return TestHelpers::pacedUntil(ticks, pred);
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("timerThread: setTimeout fires the callback once") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    t.setTimeout([&count] { count++; }, 10);
    CHECK(waitTicks([&] { return count.load() >= 1; }));
    // give the worker a beat to confirm no second tick arrives
    TestHelpers::paceWindow(40);
    CHECK(count.load() == 1);
  }

  TEST_CASE("timerThread: setInterval fires periodically") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    auto id = t.setInterval([&count] { count++; }, 10);
    CHECK(waitTicks([&] { return count.load() >= 3; }, 500));
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: Add returns increasing IDs and respects period") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> a{0}, b{0};
    auto id1 = t.Add(5, 0, [&a] { a++; });
    auto id2 = t.Add(5, 0, [&b] { b++; });
    CHECK(id1 != id2);
    CHECK(waitTicks([&] { return a.load() == 1 && b.load() == 1; }));
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
    TestHelpers::paceWindow(20);
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

    REQUIRE(waitTicks([&] { return entered.load(); }));

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
    TestHelpers::paceWindow(20);
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
    TestHelpers::paceWindow(20);
    REQUIRE(count.load() == 0);

    // At least 20ms of the cycle has elapsed and the new period is 10ms, so the
    // target expiry is already in the past — the timer must fire promptly rather
    // than wait out a fresh 10ms, and must not skip the beat entirely.
    CHECK(t.SetPeriod(id, 10));
    CHECK(waitTicks([&] { return count.load() >= 5; }, 1000));
    t.ClearTimer(id);
  }

  TEST_CASE("timerThread: SetPeriod slows a pending interval down") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    auto id = t.setInterval([&count] { count++; }, 10);
    REQUIRE(waitTicks([&] { return count.load() >= 2; }, 1000));

    CHECK(t.SetPeriod(id, 5000));
    // Let any tick already in flight land, then the queue must go quiet.
    TestHelpers::paceWindow(30);
    const int settled = count.load();
    TestHelpers::paceWindow(150);
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
    CHECK(waitTicks([&] { return count.load() >= 5; }, 1200));
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
    CHECK(waitTicks([&] { return a.load() >= 3; }, 1000));
    CHECK(b.load() == 0); // idB keeps its own 5s interval

    t.ClearTimer(idA);
    t.ClearTimer(idB);
  }

  // ─── retirement from inside a callback (issue #722) ───────────────────────
  //
  // `destroyImpl` blocks on the worker reporting an in-flight callback finished.
  // Asked for from *inside* that callback the wait is on the calling thread's
  // own completion: the worker never comes back, and one worker serves every
  // timer in the process, so the whole clock stops. #721 kept `.metro` — the
  // only consumer — off the path with a thread-local frame marker of its own;
  // #722 makes the primitive answer the question itself, from the one fact only
  // it has, which is whether the caller is the thread it spawned.
  //
  // A deadlock cannot be tested with a blocking join, so each case below is a
  // bounded poll on a flag the callback sets *after* the call returns. On a
  // regression the flag never flips, the CHECK prints, and the timer object is
  // deliberately leaked rather than destroyed — `~timerThread` joins the worker,
  // and joining a parked worker would hang the run instead of reporting it.

  TEST_CASE("timerThread: ClearTimer from inside its own callback retires it (#722)") {
    auto* t = new YSE::PATCHER::timerThread();
    std::atomic<int> ticks{0};
    std::atomic<bool> cleared{false};
    std::atomic<bool> first{false};
    std::atomic<bool> second{false};
    std::atomic<YSE::PATCHER::timerThread::timerID> self{0};

    auto id = t->setInterval(
        [&] {
          ticks++;
          const auto me = self.load();
          if (me == 0) return; // the store below has not landed yet
          // The call that used to park this thread for good.
          first.store(t->ClearTimer(me));
          // And again: a second request for a timer already retiring must be a
          // no-op rather than reaching the idle branch, which would erase the
          // node holding the std::function running right now.
          second.store(t->ClearTimer(me));
          cleared.store(true);
        },
        5);
    self.store(id);

    const bool returned = waitTicks([&] { return cleared.load(); }, 2000);
    CHECK(returned);
    if (!returned) return; // leaked on purpose — see the note above
    std::unique_ptr<YSE::PATCHER::timerThread> owned(t);

    CHECK(first.load()); // the id named a live timer
    CHECK(second.load()); // still true: it is retiring, not unknown
    // Retired by the worker the moment the callback returned, so the promise
    // that matters — no callback begins after this — holds.
    CHECK(waitTicks([&] { return owned->empty(); }, 1000));
    const int settled = ticks.load();
    TestHelpers::paceWindow(80);
    CHECK(ticks.load() == settled);
  }

  TEST_CASE("timerThread: Clear() from inside a callback sweeps and returns (#722)") {
    // `Clear()` loops on `active.begin()` until the map is empty. The timer
    // running the caller cannot leave that map before this callback returns, so
    // the loop had to learn to step over it — otherwise the deferred cancel just
    // turns the deadlock into a spin, with `sync` held.
    auto* t = new YSE::PATCHER::timerThread();
    std::atomic<bool> done{false};
    std::atomic<bool> arm{false};
    std::atomic<int> other{0};

    t->Add(5000, 0, [&other] { other++; }); // something for the sweep to find
    t->setInterval(
        [&] {
          if (!arm.load()) return;
          t->Clear();
          done.store(true);
        },
        5);
    arm.store(true);

    const bool returned = waitTicks([&] { return done.load(); }, 2000);
    CHECK(returned);
    if (!returned) return; // leaked on purpose — see the note above
    std::unique_ptr<YSE::PATCHER::timerThread> owned(t);

    // The sweep took the other timer; the one that ran it retires on return.
    CHECK(waitTicks([&] { return owned->empty(); }, 1000));
    CHECK(other.load() == 0);
  }

  TEST_CASE("timerThread: two threads clearing one running timer both wait it out (#722)") {
    // Both find the callback in flight. Before #722 the second saw `running`
    // already cleared by the first and took the idle branch: it erased the node
    // the worker was still executing out of, and freed it under the first
    // caller's wait. Now the retirement is a state of the timer rather than a
    // side effect on `running`, and both waiters watch the same id disappear.
    // Everything the two clearing threads touch is on the heap and captured by
    // value, so the bounded poll below can give up and leave them detached
    // rather than joining threads that may never come back.
    auto* t = new YSE::PATCHER::timerThread();
    auto* returned = new std::atomic<int>(0);
    auto* entered = new std::atomic<bool>(false);
    auto* mayExit = new std::atomic<bool>(false);

    auto id = t->setInterval(
        [entered, mayExit] {
          entered->store(true);
          while (!mayExit->load())
            std::this_thread::sleep_for(1ms);
        },
        1);
    REQUIRE(waitTicks([&] { return entered->load(); }));

    std::thread a([t, id, returned] {
      t->ClearTimer(id);
      (*returned)++;
    });
    std::thread b([t, id, returned] {
      t->ClearTimer(id);
      (*returned)++;
    });

    // Neither may report the timer stopped while it is demonstrably running.
    TestHelpers::paceWindow(20);
    CHECK(returned->load() == 0);

    mayExit->store(true);
    const bool both = waitTicks([&] { return returned->load() == 2; }, 2000);
    CHECK(both);
    if (!both) {
      a.detach();
      b.detach();
      return; // leaked on purpose — see the note above
    }
    a.join();
    b.join();
    CHECK(t->empty());
    delete t;
    delete mayExit;
    delete entered;
    delete returned;
  }

  TEST_CASE("timerThread: a neighbour timer survives a clear from inside a callback (#722)") {
    // The collateral half of the same frame: retiring one timer from inside a
    // callback must not disturb any other, and the rest of the queue must keep
    // running on the same worker afterwards.
    auto* t = new YSE::PATCHER::timerThread();
    std::atomic<int> neighbour{0};
    std::atomic<bool> cleared{false};
    std::atomic<YSE::PATCHER::timerThread::timerID> self{0};

    const auto other = t->setInterval([&neighbour] { neighbour++; }, 5);
    auto id = t->setInterval(
        [&] {
          const auto me = self.load();
          if (me == 0) return;
          t->ClearTimer(me);
          cleared.store(true);
        },
        5);
    self.store(id);

    const bool returned = waitTicks([&] { return cleared.load(); }, 2000);
    CHECK(returned);
    if (!returned) return; // leaked on purpose — see the note above
    std::unique_ptr<YSE::PATCHER::timerThread> owned(t);

    const int base = neighbour.load();
    CHECK(waitTicks([&] { return neighbour.load() > base + 2; }, 2000));
    CHECK(owned->size() == 1);
    owned->ClearTimer(other);
  }

  TEST_CASE("timerThread: setInterval with chrono duration overloads") {
    YSE::PATCHER::timerThread t;
    std::atomic<int> count{0};
    YSE::PATCHER::timerThread::boundHandlerType<> cb = [&count] { count++; };
    auto id = t.Add(std::chrono::milliseconds(5), std::chrono::milliseconds(10), cb);
    CHECK(waitTicks([&] { return count.load() >= 2; }, 500));
    t.ClearTimer(id);
  }

} // TEST_SUITE("patcher")
