
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

      // Retire a timer. Unlike SetPeriod above, this **blocks** when the
      // timer's callback is in flight: it waits on a condition variable until
      // the worker reports the callback finished, which is the handshake that
      // lets an owner be destroyed while its timer is firing.
      //
      // It therefore must NOT be called from inside that timer's own callback —
      // the wait would be on this thread's own completion, and the worker (one
      // per process) never comes back. The note beside SetPeriod says which
      // calls are callback-safe; this is the one that is not (issue #721).
      // `.metro`, the only consumer, keeps off this path from inside `Bang()`
      // by taking timerBridge's wait-free route there.
      bool ClearTimer(timerID id);
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

        // you must be holding the sync lock to assign wait cond
        std::unique_ptr<ConditionVar> waitCond;

        bool running = false;
        // Set by the worker before notify_all() on the cancellation path; the
        // destroyImpl predicate checks this to guard against spurious wakeup
        // (cpp:S5404). The worker no longer erases from `active` itself —
        // destroyImpl does, so this Timer is alive across the predicate read.
        bool destroyed = false;
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

      timerID nextId;
      TimerMap active;
      Queue queue;

      mutable Lock sync;
      ConditionVar wakeUp;
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
