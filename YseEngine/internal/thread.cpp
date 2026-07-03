/*
  ==============================================================================

    thread.cpp
    Created: 1 Oct 2014 12:37:48pm
    Author:  yvan

  ==============================================================================
*/

#include "thread.h"
#include <assert.h>
#include "time.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

YSE::INTERNAL::thread::thread()
: shouldExit(false) {
}

YSE::INTERNAL::thread::~thread()
{
  assert(!isRunning());
  stop();
}

void YSE::INTERNAL::thread::start() {
  if (!handle) {
    shouldExit = false;
    handle.reset(new std::thread(&thread::run, this));
  }
}

bool YSE::INTERNAL::thread::isRunning() const {
  return (handle) ? true : false;
}

void YSE::INTERNAL::thread::stop() {

  if (handle) {
    // you cannot stop a thread from within that thread!
    assert(handle->get_id() != std::this_thread::get_id());

    shouldExit = true;

    if (handle->joinable()) {
      handle->join();
      handle.reset();
    }
  }
}

void YSE::INTERNAL::thread::setPriority(bool high) {
  if (!handle) return;
#if defined(_WIN32)
  ::SetThreadPriority(static_cast<HANDLE>(handle->native_handle()),
                      high ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL);
#else
  // Best-effort: raising to SCHED_FIFO needs privileges (CAP_SYS_NICE / RT
  // limits). If pthread_setschedparam fails we leave the thread at its default
  // policy — the spin-based join() still bounds the callback stall, priority is
  // only an optimisation against preemption.
  int policy = high ? SCHED_FIFO : SCHED_OTHER;
  sched_param param{};
  if (high) {
    int lo = ::sched_get_priority_min(SCHED_FIFO);
    int hi = ::sched_get_priority_max(SCHED_FIFO);
    param.sched_priority = (lo >= 0 && hi >= 0) ? lo + (hi - lo) / 2 : 0;
  }
  ::pthread_setschedparam(handle->native_handle(), policy, &param);
#endif
}

bool YSE::INTERNAL::thread::threadShouldExit() const {
  return shouldExit;
}