#include "TimerThread.h"
#include <cassert>

using namespace YSE::PATCHER;

timerThread & YSE::PATCHER::TimerThread() {
  static timerThread s;
  return s;
}

void timerThread::timerThreadWorker() {
  ScopedLock lock(sync);

  while (!done) {
    if (queue.empty()) {
      //wait for done or work
      wakeUp.wait(lock, [this] {
        return done || !queue.empty();
      });
      continue;
    }

    auto queueHead = queue.begin();
    Timer & timer = *queueHead;
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
        }
        else {
          active.erase(timer.id);
        }
      }
      else {
        // timer has stopped, notify thread
        timer.waitCond->notify_all();
        active.erase(timer.id);
      }
    }
    else {
      wakeUp.wait_until(lock, timer.next);
    }
  }
}

timerThread::timerThread()
  : nextId(noTimer + 1)
  , queue()
  , done(false) {
}

timerThread::~timerThread() {
  ScopedLock lock(sync);

  if (worker.joinable()) {
    done = true;
    lock.unlock();
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
  ScopedLock lock(sync);

  // start timer if not running
  if (!worker.joinable()) {
    worker = std::thread(&timerThread::timerThreadWorker, this);
  }

  auto id = nextId++;
  auto iter = active.emplace(id, Timer(id, Clock::now() + Duration(msDelay), Duration(msPeriod), std::move(func)));

  Queue::iterator place = queue.emplace(iter.first->second);

  // notify if in front of queue
  bool needNotify = (place == queue.begin());

  lock.unlock();

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

std::size_t timerThread::size() const noexcept
{
  ScopedLock lock(sync);
  return active.size();
}

bool timerThread::empty() const noexcept {
  ScopedLock lock(sync);
  return active.empty();
}

// if notify is true, returns with lock unlocked
bool timerThread::destroyImpl(ScopedLock & lock, timerThread::TimerMap::iterator i, bool notify) {
  assert(lock.owns_lock());

  if (i == active.end()) return false;

  Timer & timer = i->second;

  if (timer.running) {
    // if callback in progress, flag for deletion
    timer.running = false;

    // assign a condition variable
    timer.waitCond.reset(new ConditionVar);
    
    // block until the callback is finished
    timer.waitCond->wait(lock);
  }
  else {
    queue.erase(timer);
    active.erase(i);

    if (notify) {
      lock.unlock();
      wakeUp.notify_all();
    }
  }
  return true;
}

timerThread::Timer::Timer(timerThread::timerID id)
  : id(id)
  , running(false)
{}

timerThread::Timer::Timer(Timer && r) noexcept
  : id(std::move(r.id))
  , next(std::move(r.next))
  , period(std::move(r.period))
  , func(std::move(r.func))
{}

timerThread::Timer::Timer(timerThread::timerID id, Timestamp next, Duration period, timerFunc func) noexcept
  : id(id)
  , next(next)
  , period(period)
  , func(std::move(func))
  , running(false)
{}