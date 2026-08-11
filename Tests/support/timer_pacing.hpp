#pragma once
// Pacing a test against the machine, rather than against a clock.
//
// Any case that lets the real `timerThread` deliver used to wait for N ticks
// inside a fixed wall-clock budget (`waitFor(pred, 2000)`). That is a statement
// about the machine's scheduler and not about the object — a saturated box
// falsifies it while the object behaves perfectly — which is issues #751 and
// #752, and #747, #740 and #739 before them: a wait that bounds the wrong
// thing.
//
// Everything that can be *joined* should be, with the handshakes the engine
// publishes: a control-thread start or stop goes through `timerBridge`'s
// blocking front, so `timerThread::size()` is final on return, and
// `timerBridge::WaitIdle()` joins a deferred reconcile. But *delivery* is what
// the run cases of `.metro` and `.clocker` are about, and there is no handshake
// for "the timer has ticked N times".
//
// So the budget is replaced by a **reference timer**: an ordinary periodic
// timer armed on the same `timerThread`, at the same interval, next to the
// object's own. One worker fires both out of one deadline-sorted queue, so
// whatever starves the object's timer starves this one identically, and a case
// can ask for a number of *ticks* instead of a number of milliseconds.
//
// Two of them sandwich the object exactly. The worker fires strictly in
// deadline order, so of two timers of the same period the **older** has passed
// at least as many deadlines as the younger at every instant: a reference armed
// just *before* the object's timer is an upper bound on the object's output and
// one armed just *after* it is a lower bound, on any box, with no slack term at
// all. Sample the younger before the object's count and the older after it, and
// a test thread descheduled between the reads can only widen the sandwich
// rather than skew it.
//
// The same construction also replaces the inverted bet — `sleep_for(200ms)` as
// a "nothing more arrived" window, which a loaded box makes vacuously true.
// Waiting out N ticks of a reference is a window that *grows* with the load,
// and it is the window the object's own timer would have fired in had it still
// been armed.
//
// A stalled reference is still a failure: a periodic millisecond timer that has
// delivered nothing for this long is a wedged worker rather than a busy one,
// which is a different diagnosis and worth reporting as one rather than waiting
// out. Note that this bounds a *stall* and not the total wait — a box a
// thousand times slower simply takes a thousand times longer and passes.
//
// Extracted from test_patcher_clocker.cpp (issue #751) when `.metro`'s run
// cases needed the same instrument (issue #752).

#include <atomic>
#include <chrono>
#include <thread>

#include "patcher/time/TimerThread.h"

namespace TestHelpers {

  constexpr auto kTimerStall = std::chrono::seconds(5);

  class refTimer {
  public:
    explicit refTimer(YSE::PATCHER::timerThread::millisec periodMs)
      : id_(YSE::PATCHER::TimerThread().Add(
            periodMs, periodMs, [this] { ticks_.fetch_add(1, std::memory_order_release); })) {}

    ~refTimer() {
      // `ClearTimer` waits out a callback already in flight, so nothing can
      // touch the counter after this returns — the same stop-means-stopped
      // handshake the stop cases assert with.
      YSE::PATCHER::TimerThread().ClearTimer(id_);
    }
    refTimer(const refTimer&) = delete;
    refTimer& operator=(const refTimer&) = delete;
    refTimer(refTimer&&) = delete;
    refTimer& operator=(refTimer&&) = delete;

    int n() const {
      return ticks_.load(std::memory_order_acquire);
    }

    // Block until this timer has delivered `total` ticks. False only when it
    // stalls — see the note above.
    bool WaitTicks(int total) {
      int seen = n();
      auto moved = std::chrono::steady_clock::now();
      while (seen < total) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int now = n();
        if (now != seen) {
          seen = now;
          moved = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - moved > kTimerStall) {
          return false;
        }
      }
      return true;
    }

  private:
    std::atomic<int> ticks_{0};
    YSE::PATCHER::timerThread::timerID id_;
  };

  // Wait for `pred`, budgeted in `ref`'s ticks rather than in milliseconds: the
  // object gets that many of the worker's own deliveries to satisfy it, however
  // long the box takes to produce them. Returns `pred()`, so a reference that
  // stalls fails the assertion the caller wrote rather than a separate one.
  template <typename P> bool waitPaced(refTimer& ref, int ticks, P pred) {
    int seen = ref.n();
    const int deadline = seen + ticks;
    auto moved = std::chrono::steady_clock::now();
    while (!pred()) {
      if (seen >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const int now = ref.n();
      if (now != seen) {
        seen = now;
        moved = std::chrono::steady_clock::now();
      } else if (std::chrono::steady_clock::now() - moved > kTimerStall) {
        return pred();
      }
    }
    return true;
  }

} // namespace TestHelpers
