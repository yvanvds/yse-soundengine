
#include "TimerThread.h"
#include <cassert>

using namespace YSE::PATCHER;

timerThread& YSE::PATCHER::TimerThread() {
  static timerThread s;
  return s;
}

void timerThread::timerThreadWorker() {
  ScopedLock lock(sync);

  while (!done) {
    if (queue.empty()) {
      // wait for done or work
      wakeUp.wait(lock, [this] { return done || !queue.empty(); });
      continue;
    }

    auto queueHead = queue.begin();
    Timer& timer = *queueHead;
    auto now = Clock::now();
    if (now >= timer.next) {
      queue.erase(queueHead);
      timer.running = true;

      lock.unlock();
      timer.func(); // execute timer
      lock.lock();

      if (timer.running) {
        timer.running = false;

        // if periodic, schedule again
        if (timer.period.count() > 0) {
          timer.next = timer.next + timer.period;
          queue.emplace(timer);
        } else {
          active.erase(timer.id);
        }
      } else {
        // timer has stopped — flag completion and notify the destroyImpl
        // waiter. destroyImpl owns the active.erase to keep this Timer alive
        // until its predicate-checked wait observes `destroyed == true`.
        timer.destroyed = true;
        timer.waitCond->notify_all();
      }
    } else {
      // Copy the deadline onto the stack before waiting: `timer` references a
      // node of `active`, and a concurrent ClearTimer/Clear can erase (free)
      // that node while wait_until has the lock released. wait_until re-reads
      // the time_point after re-acquiring the lock to compute its return
      // status, so passing timer.next directly is a use-after-free (#240).
      Timestamp deadline = timer.next;
      wakeUp.wait_until(lock, deadline);
    }
  }
}

timerThread::timerThread() : nextId(noTimer + 1), queue(), done(false) {}

timerThread::~timerThread() {
  // The locked region gets its own scope so RAII releases the mutex on every
  // path, rather than an explicit unlock() inside the branch that leaves this
  // function's lock/unlock counts unbalanced (cpp:S8473). Ordering is
  // unchanged: `done` is set under the lock, the mutex is released, and only
  // then is the worker woken — so it never wakes onto a mutex we still hold.
  bool needJoin;
  {
    ScopedLock lock(sync);
    needJoin = worker.joinable();
    if (needJoin) done = true;
  }

  if (needJoin) {
    wakeUp.notify_all();
    worker.join();
  }
}

timerThread::timerID timerThread::setInterval(timerFunc func, millisec period) {
  return Add(period, period, std::move(func));
}

timerThread::timerID timerThread::setTimeout(timerFunc func, millisec timeout) {
  return Add(timeout, 0, std::move(func));
}

timerThread::timerID timerThread::Add(millisec msDelay, millisec msPeriod, timerFunc func) {
  timerID id;
  bool needNotify;

  // Scoped so RAII releases the mutex on every path, rather than an explicit
  // unlock() that leaves this function's lock/unlock counts unbalanced
  // (cpp:S8473). Ordering is unchanged: the mutex is released at the closing
  // brace, before the notify below, so the worker never wakes onto a mutex we
  // still hold.
  {
    ScopedLock lock(sync);

    // start timer if not running
    if (!worker.joinable()) {
      worker = std::thread(&timerThread::timerThreadWorker, this);
    }

    id = nextId++;
    auto iter = active.emplace(
        id, Timer(id, Clock::now() + Duration(msDelay), Duration(msPeriod), std::move(func)));

    Queue::iterator place = queue.emplace(iter.first->second);

    // notify if in front of queue
    needNotify = (place == queue.begin());
  }

  if (needNotify) wakeUp.notify_all();

  return id;
}

bool timerThread::ClearTimer(timerThread::timerID id) {
  ScopedLock lock(sync);
  auto i = active.find(id);
  return destroyImpl(lock, i, true);
}

void timerThread::Clear() {
  ScopedLock lock(sync);
  while (!active.empty()) {
    destroyImpl(lock, active.begin(), queue.size() == 1);
  }
}

std::size_t timerThread::size() const noexcept {
  ScopedLock lock(sync);
  return active.size();
}

bool timerThread::empty() const noexcept {
  ScopedLock lock(sync);
  return active.empty();
}

// if notify is true, returns with lock unlocked
bool timerThread::destroyImpl(ScopedLock& lock, timerThread::TimerMap::iterator i, bool notify) {
  assert(lock.owns_lock());

  if (i == active.end()) return false;

  Timer& timer = i->second;

  if (timer.running) {
    // if callback in progress, flag for deletion
    timer.running = false;

    // assign a condition variable
    timer.waitCond = std::make_unique<ConditionVar>();

    // block until the callback is finished — predicate guards against
    // spurious wakeup. Worker sets `destroyed = true` before notify_all().
    timer.waitCond->wait(lock, [&timer] { return timer.destroyed; });

    // Worker deliberately leaves the cancelled Timer in `active` so this
    // predicate can safely read `timer.destroyed`; erase it now.
    active.erase(i);
  } else {
    queue.erase(timer);
    active.erase(i);

    if (notify) {
      // S8473: the unlock is deliberately unbalanced here and cannot be scoped
      // away as in Add()/~timerThread(). The lock belongs to the caller (it
      // arrives by reference) and the wait() above needs it, so this function
      // documents that it "returns with lock unlocked" when notify is set. The
      // mutex must be released *before* notify_all() so the worker does not
      // wake straight onto a mutex we still hold.
      lock.unlock(); // NOSONAR
      wakeUp.notify_all();
    }
  }
  return true;
}

timerThread::Timer::Timer(timerThread::timerID id) : id(id) {}

timerThread::Timer::Timer(Timer&& r) noexcept
  : id(std::move(r.id)),
    next(std::move(r.next)),
    period(std::move(r.period)),
    func(std::move(r.func)),
    running(r.running) {}

timerThread::Timer::Timer(timerThread::timerID id, Timestamp next, Duration period,
                          timerFunc func) noexcept
  : id(id), next(next), period(period), func(std::move(func)), running(false) {}
