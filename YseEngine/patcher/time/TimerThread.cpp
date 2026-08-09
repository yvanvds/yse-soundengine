
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

      timer.running = false;

      if (!timer.cancelled) {
        // if periodic, schedule again
        if (timer.period.count() > 0) {
          timer.next = timer.next + timer.period;
          queue.emplace(timer);
        } else {
          active.erase(timer.id);
        }
      } else {
        // Retired while its callback was in flight. This worker owns the erase
        // whichever thread asked for it — the node holds the `std::function`
        // that was executing until the line above — and it is the *only* place
        // a cancelled timer disappears, which is what lets a waiter's predicate
        // be "gone from `active`" rather than a flag inside a node it does not
        // own (issue #722).
        const timerID id = timer.id;
        active.erase(id);
        retired.notify_all();
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

bool timerThread::SetPeriod(timerThread::timerID id, timerThread::millisec msPeriod) {
  // A zero/negative period would turn a periodic timer into a one-shot inside
  // the worker (`period.count() > 0`), silently dropping it from `active`
  // behind the caller's back. Refuse instead.
  if (msPeriod <= 0) return false;

  bool needNotify = false;

  // Scoped for the same reason as Add(): the mutex must be released before the
  // notify below so the worker never wakes onto a lock we still hold.
  {
    ScopedLock lock(sync);
    auto i = active.find(id);
    if (i == active.end()) return false;

    Timer& timer = i->second;
    const Duration period(msPeriod);
    if (timer.period == period) return true;

    // Re-anchor on the previous expiry so the change reads as "this cycle is
    // now `period` long", not "restart the cycle from now". Read before the
    // unqueue below, which is what changes the key.
    const Timestamp previous = timer.next - timer.period;

    // Pulled out by identity — see `unqueue`. Not queued means either the
    // callback is running right now (the worker erased the queue entry before
    // invoking it and recomputes `next += period` afterwards) or the timer is
    // mid-cancellation and about to disappear. Storing the period is the whole
    // reschedule in the first case and harmless in the second.
    if (!unqueue(timer)) {
      timer.period = period;
      return true;
    }

    timer.period = period;
    const Timestamp target = previous + period;
    const Timestamp now = Clock::now();
    timer.next = target < now ? now : target;

    Queue::iterator place = queue.emplace(timer);
    // Only a move to the front needs a wake-up; a deadline pushed further out
    // lets the worker wake on the stale one, find nothing due and re-park.
    needNotify = (place == queue.begin());
  }

  if (needNotify) wakeUp.notify_all();
  return true;
}

bool timerThread::ClearTimer(timerThread::timerID id) {
  ScopedLock lock(sync);
  auto i = active.find(id);
  return destroyImpl(lock, i, true);
}

void timerThread::Clear() {
  ScopedLock lock(sync);
  // Asked once, before the sweep: the answer cannot change under us, and the
  // loop below has to know it on every round.
  const bool self = onWorkerThread();
  bool swept = false;

  for (;;) {
    // Restarted from the front each round rather than carried across: a
    // destroyImpl that waits out an in-flight callback releases the lock, so an
    // iterator kept over it means nothing.
    auto i = active.begin();
    // On the worker, the timer being retired from inside its own callback is
    // erased when that callback returns — which is after this sweep. Skipping
    // it is the only way out of a loop that would otherwise spin on a map only
    // this thread can empty, and it is not a timer left running: it is already
    // cancelled and will not tick again (issue #722).
    while (self && i != active.end() && i->second.cancelled)
      ++i;
    if (i == active.end()) break;
    destroyImpl(lock, i, false);
    swept = true;
  }

  // One wake-up for the whole sweep, once the lock is released. The old loop
  // asked destroyImpl to notify on whichever entry happened to leave a single
  // item in the queue, and destroyImpl returns *unlocked* when it does — so the
  // `while (!active.empty())` that followed read the map without the lock.
  lock.unlock();
  if (swept) wakeUp.notify_all();
}

std::size_t timerThread::size() const noexcept {
  ScopedLock lock(sync);
  return active.size();
}

bool timerThread::empty() const noexcept {
  ScopedLock lock(sync);
  return active.empty();
}

bool timerThread::onWorkerThread() const noexcept {
  return worker.get_id() == std::this_thread::get_id();
}

bool timerThread::unqueue(Timer& timer) {
  auto range = queue.equal_range(std::ref(timer));
  for (auto q = range.first; q != range.second; ++q) {
    if (&q->get() == &timer) {
      queue.erase(q);
      return true;
    }
  }
  return false;
}

// if notify is true, returns with lock unlocked
bool timerThread::destroyImpl(ScopedLock& lock, timerThread::TimerMap::iterator i, bool notify) {
  assert(lock.owns_lock());

  if (i == active.end()) return false;

  Timer& timer = i->second;

  if (timer.running || timer.cancelled) {
    // The callback is in flight, so this node is the worker's until it returns
    // — it is executing the `std::function` stored in it. Record the
    // retirement; the erase happens in the worker's post-callback branch, once,
    // however many callers ask (issue #722).
    timer.running = false;
    timer.cancelled = true;

    // The case a comment could not enforce (issue #721, filed as #722). A
    // running timer seen from the worker thread *is* the callback on this
    // stack, so the completion a wait would block for is this thread's own —
    // and the worker is one per object, so the park was permanent and took
    // every other timer with it. Recording is the whole of the work here: the
    // branch above runs the instant the callback returns.
    if (onWorkerThread()) return true;

    // Off the worker the handshake stands. The predicate names the id, not the
    // node, so it stays true after the Timer is gone and reads nothing this
    // wait does not own — which is also what makes a second concurrent clear
    // for the same id safe instead of an erase under the first one.
    const timerID id = timer.id;
    retired.wait(lock, [this, id] { return active.find(id) == active.end(); });
    return true;
  }

  // Idle: nobody else is looking at this node, so it goes now.
  unqueue(timer);
  active.erase(i);

  if (notify) {
    // S8473: the unlock is deliberately unbalanced here and cannot be scoped
    // away as in Add()/~timerThread(). The lock belongs to the caller (it
    // arrives by reference) and the wait() above needs it, so this function
    // documents that it "returns with lock unlocked" when notify is set. The
    // mutex must be released *before* notify_all() so the worker does not wake
    // straight onto a mutex we still hold.
    lock.unlock(); // NOSONAR
    wakeUp.notify_all();
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
