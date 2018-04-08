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

      template<typename... Args>
      using boundHandlerType = std::function<void(Args...)>;

      using millisec = std::int64_t;

      explicit timerThread();
      ~timerThread();

      timerID Add(millisec msDelay, millisec msPeriod, timerFunc func);

      template<typename SRep, typename SPer,
        typename PRep, typename PPer,
        typename... Args>
        timerID Add(
          typename std::chrono::duration<SRep, SPer> const & delay,
          typename std::chrono::duration<PRep, PPer> const & period,
          boundHandlerType<Args...> handler, 
          Args&& ...args);

      template<typename... Args>
      timerID Add(millisec msDelay,
        millisec msPeriod,
        boundHandlerType<Args...> handler,
        Args&& ...args);

      timerID setInterval(timerFunc func, millisec period);
      timerID setTimeout(timerFunc func, millisec period);

      template<typename... Args>
      timerID setInterval(boundHandlerType<Args...> handler, millisec period, Args&& ...args);

      template<typename... Args>
      timerID setTimeout(boundHandlerType<Args...> handler, millisec period, Args&& ...args);

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
        Timer & operator=(Timer&& r) noexcept;

        Timer(timerID id, Timestamp next, Duration period, timerFunc func) noexcept;

        Timer(Timer const & r) = delete;
        Timer(&operator=(Timer const & r)) = delete;

        timerID id;
        Timestamp next;
        Duration period;
        timerFunc func;

        // you must be holding the sync lock to assign wait cond
        std::unique_ptr<ConditionVar> waitCond;

        bool running;
      };

      // comparison functor to sort the timer queue
      struct NextActiveComparator
      {
        bool operator()(Timer const & a, Timer const & b) const noexcept {
          return a.next < b.next;
        }
      };

      // queue is set of references, sorted by next
      using QueueValue = std::reference_wrapper<Timer>;
      using Queue = std::multiset<QueueValue, NextActiveComparator>;
      using TimerMap = std::unordered_map<timerID, Timer>;

      void timerThreadWorker();
      bool destroyImpl(ScopedLock & lock, TimerMap::iterator i, bool notify);

      timerID nextId;
      TimerMap active;
      Queue queue;

      mutable Lock sync;
      ConditionVar wakeUp;
      std::thread worker;
      bool done;
    };

    timerThread & TimerThread();

    template<typename SRep, typename SPer,
      typename PRep, typename PPer,
      typename... Args>
      timerThread::timerID timerThread::Add(
        typename std::chrono::duration<SRep, SPer> const & delay,
        typename std::chrono::duration<PRep, PPer> const & period,
        boundHandlerType<Args...> handler, Args&& ...args) {

      millisec msDelay = std::chrono::duration_cast<
        std::chrono::milliseconds>(delay).count();

      millisec msPeriod = std::chrono::duration_cast<
        std::chrono::milliseconds>(period).count();

      return Add(msDelay, msPeriod, std::move(handler), std::forward<Args>(args)...);
    }

    template<typename... Args>
    timerThread::timerID timerThread::Add(
      millisec msDelay,
      millisec msPeriod,
      boundHandlerType<Args...> handler,
      Args&& ...args)
    {
      return Add(msDelay, msPeriod,
        std::bind(std::move(handler),
          std::forward<Args>(args)...));
    }

    // Javascript-like setInterval
    template<typename... Args>
    timerThread::timerID timerThread::setInterval(
      boundHandlerType<Args...> handler,
      millisec period,
      Args&& ...args)
    {
      return setInterval(std::bind(std::move(handler),
        std::forward<Args>(args)...),
        period);
    }

    // Javascript-like setTimeout
    template<typename... Args>
    timerThread::timerID timerThread::setTimeout(
      boundHandlerType<Args...> handler,
      millisec timeout,
      Args&& ...args)
    {
      return setTimeout(std::bind(std::move(handler),
        std::forward<Args>(args)...),
        timeout);
    }
  }
}