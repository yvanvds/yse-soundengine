/*
  ==============================================================================

    thread.h
    Created: 1 Oct 2014 12:37:48pm
    Author:  yvan

  ==============================================================================
*/

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED


#include "../headers/types.hpp"
#include <thread>
#include <memory>

// TODO: look into nativeHandle set threadpriorities when supported

namespace YSE {
  namespace INTERNAL {

    class thread {
    public:

      thread();
      virtual ~thread();

      virtual void run() = 0;

      void start();
      void stop();

      // Best-effort thread priority. `high` raises this thread toward the
      // audio-callback priority band (SCHED_FIFO on POSIX / THREAD_PRIORITY_-
      // HIGHEST on Windows) so a render worker can't be preempted by ordinary
      // work while the callback spins in threadPoolJob::join() waiting for it
      // (issue #188). Must be called after start(); a no-op if the underlying
      // handle isn't running yet. Failure (e.g. POSIX RT scheduling denied to
      // an unprivileged process) is silently ignored — correctness never
      // depends on the priority actually taking effect.
      void setPriority(bool high);

      bool isRunning() const;
      bool threadShouldExit() const;

    private:
      std::shared_ptr<std::thread> handle;
      aBool shouldExit;
    };

  }
}






#endif  // THREAD_H_INCLUDED
