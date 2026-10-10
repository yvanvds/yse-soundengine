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

  // ── A suite-wide pacing reference (issue #753) ────────────────────────────
  //
  // The sandwich above needs a reference on the *same* worker as the object
  // under test, which only the patcher's own timer cases can arrange. The rest
  // of the suite waits on other workers entirely — the script thread, the sound
  // manager's slow pool, the synth setup job, the MIDI sender — and had no
  // reference at all, so every one of those waits was denominated in wall clock:
  // `waitFor(pred, 2000)`, `for (i < 300) { update(); sleep(10ms); }`, or a bare
  // `sleep_for(200ms)` standing in for "and nothing more arrived". All three
  // assert that the machine kept up, which is issue #753's inventory.
  //
  // What those cases need is not the sandwich but the *pace*: a budget counted
  // in deliveries of a periodic timer instead of in milliseconds, so a box a
  // hundred times slower takes a hundred times longer and still passes, while a
  // worker that has genuinely wedged still fails. One free-running 1 ms timer
  // serves the whole suite for that, so a wait costs an atomic load rather than
  // arming a timer of its own — the settle-margin shapes appear inside hot loops
  // where arming per call would be the dominant cost.
  //
  // It runs on a `timerThread` of the suite's own rather than on the engine's
  // singleton, because `System().close()` sweeps that singleton
  // (system.cpp:222) and would silently disarm a pacing timer mid-wait in every
  // case that closes the engine. It is likewise invisible to the
  // `TimerThread().size()` assertions in the patcher suite, and its worker never
  // allocates on a thread an `AllocProbe` is watching (the probe counts per
  // thread since #701).
  //
  // A stall of the reference is still a failure for the same reason as above: a
  // 1 ms timer that has delivered nothing for five seconds is a wedged worker,
  // not a busy one. Note again that this bounds a stall and not the total wait.

  namespace detail {

    class paceSource {
    public:
      paceSource()
        : id_(worker_.Add(1, 1, [this] { ticks_.fetch_add(1, std::memory_order_release); })) {}

      ~paceSource() {
        worker_.ClearTimer(id_);
      }
      paceSource(const paceSource&) = delete;
      paceSource& operator=(const paceSource&) = delete;
      paceSource(paceSource&&) = delete;
      paceSource& operator=(paceSource&&) = delete;

      long n() const {
        return ticks_.load(std::memory_order_acquire);
      }

    private:
      YSE::PATCHER::timerThread worker_;
      std::atomic<long> ticks_{0};
      YSE::PATCHER::timerThread::timerID id_;
    };

    inline paceSource& Pace() {
      static paceSource s;
      return s;
    }

  } // namespace detail

  // Deliveries of the suite's reference timer so far. Monotonic.
  inline long paceTicks() {
    return detail::Pace().n();
  }

  // Wait out a window of `ticks` further deliveries: the load-proportional
  // replacement for a fixed sleep used as a settle margin or as a "nothing more
  // arrived" window. Returns early only if the reference stalls, and a run whose
  // reference has stalled has worse problems than this window.
  inline void paceWindow(int ticks) {
    long seen = paceTicks();
    const long deadline = seen + ticks;
    auto moved = std::chrono::steady_clock::now();
    while (seen < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const long now = paceTicks();
      if (now != seen) {
        seen = now;
        moved = std::chrono::steady_clock::now();
      } else if (std::chrono::steady_clock::now() - moved > kTimerStall) {
        return;
      }
    }
  }

  // Wait for `pred`, budgeted in reference ticks rather than in milliseconds.
  // Returns `pred()`, so a stalled reference fails the caller's own assertion
  // rather than a separate one.
  template <typename P> bool pacedUntil(int ticks, P pred) {
    long seen = paceTicks();
    const long deadline = seen + ticks;
    auto moved = std::chrono::steady_clock::now();
    while (!pred()) {
      if (seen >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const long now = paceTicks();
      if (now != seen) {
        seen = now;
        moved = std::chrono::steady_clock::now();
      } else if (std::chrono::steady_clock::now() - moved > kTimerStall) {
        return pred();
      }
    }
    return true;
  }

  // As above, for the waits whose progress needs the test thread to do
  // something each round — `System().update()` for anything that reaches the
  // engine's main tick. `step()` runs once per poll, `pollMs` apart.
  //
  // `pollMs = 0` does not sleep at all, for the loops whose progress *is* the
  // stepping: a case pumping the offline engine block by block gets as many
  // blocks per second as the box can render, and inserting even a 1 ms sleep
  // between them would change how much audio the budget buys. Those loops are
  // still bounded, in reference ticks, and still stall-guarded.
  template <typename P, typename S> bool pacedPump(int ticks, P pred, S step, int pollMs = 1) {
    long seen = paceTicks();
    const long deadline = seen + ticks;
    auto moved = std::chrono::steady_clock::now();
    while (!pred()) {
      if (seen >= deadline) return false;
      step();
      if (pollMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
      const long now = paceTicks();
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
