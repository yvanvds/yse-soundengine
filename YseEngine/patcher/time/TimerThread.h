
#pragma once
#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <mutex>
#include <condition_variable>

namespace YSE {
  namespace PATCHER {

    class timerThread {
    public:
      using timerID = std::uint64_t;
      static timerID constexpr noTimer = timerID(0);
      using timerFunc = std::function<void()>;

      template <typename... Args> using boundHandlerType = std::function<void(Args...)>;

      using millisec = std::int64_t;

      explicit timerThread();
      ~timerThread();

      timerID Add(millisec msDelay, millisec msPeriod, timerFunc func);

      template <typename SRep, typename SPer, typename PRep, typename PPer, typename... Args>
      timerID Add(typename std::chrono::duration<SRep, SPer> const& delay,
                  typename std::chrono::duration<PRep, PPer> const& period,
                  boundHandlerType<Args...> handler, Args&&... args);

      template <typename... Args>
      timerID Add(millisec msDelay, millisec msPeriod, boundHandlerType<Args...> handler,
                  Args&&... args);

      timerID setInterval(timerFunc func, millisec period);
      timerID setTimeout(timerFunc func, millisec period);

      template <typename... Args>
      timerID setInterval(boundHandlerType<Args...> handler, millisec period, Args&&... args);

      template <typename... Args>
      timerID setTimeout(boundHandlerType<Args...> handler, millisec period, Args&&... args);

      // Change the interval of an already-scheduled periodic timer (issue
      // #625). Without this a caller can only restart the timer, which loses
      // the phase of the cycle it is in and — for `.metro` — was impossible
      // from inside the callback at all.
      //
      // Rescheduling rule: the next expiry moves to *previous expiry +
      // msPeriod*. When the period shrinks past the part of the current cycle
      // that has already elapsed, that instant is in the past; the timer then
      // fires as soon as the worker wakes rather than skipping the beat. A
      // period that grows simply pushes the pending expiry out.
      //
      // Callable from any thread, *including from inside the timer's own
      // callback* — the worker releases the lock across the callback, so there
      // is no self-deadlock. In that case the timer is not queued and only the
      // stored period is updated; the worker's own `next += period` reschedule
      // then applies the same rule.
      //
      // Returns false for an unknown id or a non-positive period (a periodic
      // timer is never silently degraded into a one-shot), true otherwise.
      bool SetPeriod(timerID id, millisec msPeriod);

      // Retire a timer, and the one guarantee that holds however it is called:
      // **no callback for this id begins after this returns**.
      //
      // What it costs depends on where the caller stands, and the class works
      // that out itself rather than asking the caller to know (issue #722):
      //
      //  - **Off the timer worker**, with the callback in flight, it *blocks*
      //    until the worker has finished that callback and dropped the timer —
      //    the stop-means-stopped handshake that lets an owner be destroyed
      //    while its timer is firing. Any number of threads may ask at once;
      //    they all wait out the same retirement.
      //  - **From inside the timer's own callback** it records the retirement
      //    and returns. There is nothing to wait for: the completion a wait
      //    would block on is the calling thread's, and until #722 that wait was
      //    a permanent park of the one worker in the process (issue #721).
      //    The timer is retired by the worker the moment the callback returns,
      //    so no further tick comes out; what the caller does *not* get is a
      //    promise that no callback is running, since one is — its own.
      //
      // Still not for the audio callback: it takes a mutex, and off the worker
      // it blocks. `timerBridge` (#718) is what a real-time caller uses.
      //
      // Returns false only for an id no live timer has.
      bool ClearTimer(timerID id);

      // Retire every timer. Blocks out in-flight callbacks exactly as
      // ClearTimer does, and is callable from inside a callback for the same
      // reason: the timer running the caller is retired on return rather than
      // waited for, so the sweep cannot spin on a map only this thread can
      // empty (issue #722).
      void Clear();

      std::size_t size() const noexcept;
      bool empty() const noexcept;

    private:
      using Lock = std::mutex;
      using ScopedLock = std::unique_lock<Lock>;
      using ConditionVar = std::condition_variable;

      using Clock = std::chrono::steady_clock;
      using Timestamp = std::chrono::time_point<Clock>;
      using Duration = std::chrono::milliseconds;

      struct Timer {
        explicit Timer(timerID id = 0);
        Timer(Timer&& r) noexcept;
        Timer& operator=(Timer&& r) noexcept;

        Timer(timerID id, Timestamp next, Duration period, timerFunc func) noexcept;

        Timer(Timer const& r) = delete;
        Timer(&operator=(Timer const& r)) = delete;

        timerID id;
        Timestamp next;
        Duration period;
        timerFunc func;

        // The worker is inside `func()` right now. Set and cleared by the
        // worker only, under `sync`.
        bool running = false;
        // Retirement has been asked for while `func()` was in flight. The node
        // then belongs to the worker until the callback returns — it holds the
        // `std::function` that is executing — so nothing else may erase it, and
        // a second or third request for the same id has nothing left to do but
        // wait for (or, on the worker, hand over) the same retirement. Written
        // under `sync` (issue #722).
        bool cancelled = false;
      };

      // comparison functor to sort the timer queue
      struct NextActiveComparator {
        bool operator()(Timer const& a, Timer const& b) const noexcept {
          return a.next < b.next;
        }
      };

      // queue is set of references, sorted by next
      using QueueValue = std::reference_wrapper<Timer>;
      using Queue = std::multiset<QueueValue, NextActiveComparator>;
      using TimerMap = std::unordered_map<timerID, Timer>;

      void timerThreadWorker();
      bool destroyImpl(ScopedLock& lock, TimerMap::iterator i, bool notify);

      // Whether the caller is the thread that runs this object's callbacks —
      // the question `ClearTimer` could previously only answer by asking the
      // caller to know (issue #722). Combined with `Timer::running` it is
      // exact: the worker is a single thread this object spawned and it runs
      // one callback at a time, so a running timer seen *from* the worker is by
      // construction the one whose callback is on this stack.
      //
      // A member rather than a thread_local, unlike
      // `patcherImplementation::CallingThread` (#690) and `.metro`'s bang frame
      // (#721): the question is per *instance* — the tests build their own
      // timers, and a callback on one object's worker may legitimately block on
      // another's — so a thread_local would have to carry the instance anyway.
      // `sync` must be held: `worker` is assigned under it.
      bool onWorkerThread() const noexcept;

      // Drop `timer`'s entry from the queue, matched on identity. Returns false
      // when it was not queued (its callback is running, or it is retiring).
      // `queue.erase(timer)` is the by-key overload and would take *every*
      // entry with an equivalent deadline — the multiset is keyed on `next`, so
      // two timers due at the same instant are equivalent keys and clearing one
      // would silently unschedule the other. `sync` held.
      bool unqueue(Timer& timer);

      timerID nextId;
      TimerMap active;
      Queue queue;

      mutable Lock sync;
      ConditionVar wakeUp;
      // Signalled by the worker when it retires a cancelled timer. One per
      // object rather than one per timer: waiters name the *id* they are
      // waiting for and re-check `active`, so nothing dereferences a Timer node
      // it does not own and any number of them may wait on one retirement.
      ConditionVar retired;
      std::thread worker;
      bool done;
    };

    timerThread& TimerThread();

    template <typename SRep, typename SPer, typename PRep, typename PPer, typename... Args>
    timerThread::timerID timerThread::Add(typename std::chrono::duration<SRep, SPer> const& delay,
                                          typename std::chrono::duration<PRep, PPer> const& period,
                                          boundHandlerType<Args...> handler, Args&&... args) {

      millisec msDelay = std::chrono::duration_cast<std::chrono::milliseconds>(delay).count();

      millisec msPeriod = std::chrono::duration_cast<std::chrono::milliseconds>(period).count();

      return Add(msDelay, msPeriod, std::move(handler), std::forward<Args>(args)...);
    }

    template <typename... Args>
    timerThread::timerID timerThread::Add(millisec msDelay, millisec msPeriod,
                                          boundHandlerType<Args...> handler, Args&&... args) {
      return Add(msDelay, msPeriod, std::bind(std::move(handler), std::forward<Args>(args)...));
    }

    // Javascript-like setInterval
    template <typename... Args>
    timerThread::timerID timerThread::setInterval(boundHandlerType<Args...> handler,
                                                  millisec period, Args&&... args) {
      return setInterval(std::bind(std::move(handler), std::forward<Args>(args)...), period);
    }

    // Javascript-like setTimeout
    template <typename... Args>
    timerThread::timerID timerThread::setTimeout(boundHandlerType<Args...> handler,
                                                 millisec timeout, Args&&... args) {
      return setTimeout(std::bind(std::move(handler), std::forward<Args>(args)...), timeout);
    }
  } // namespace PATCHER
} // namespace YSE
