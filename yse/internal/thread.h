/*
  ==============================================================================

    thread.h
    Created: 11 Jun 2014 3:49:04pm
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

      bool isRunning() const;
      bool threadShouldExit() const;

    private:
      std::shared_ptr<std::thread> handle;
      aBool shouldExit;
    };

  }
}



#endif  // THREAD_H_INCLUDED
