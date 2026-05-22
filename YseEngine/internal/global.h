/*
  ==============================================================================

    global.h
    Created: 27 Jan 2014 10:18:27pm
    Author:  yvan

  ==============================================================================
*/

#ifndef GLOBAL_H_INCLUDED
#define GLOBAL_H_INCLUDED

#include <memory>

#include "../headers/types.hpp"
#include "../classes.hpp"
#include "threadPool.h"

namespace YSE {

  namespace INTERNAL {

    class NamedBus;

    class global {
    public:
      bool isActive() { return active; }

      // True between the end of system::initShared() and the start of
      // system::close(). Within a session, SAMPLERATE is immutable: the device
      // writers in portaudioDeviceManager.cpp / oboeImplementation.cpp assert
      // !isSampleRateLocked() before mutating SAMPLERATE. Lookup tables and
      // other SAMPLERATE-derived caches across the engine rely on this
      // contract.
      bool isSampleRateLocked() { return sampleRateLocked; }

      void addSlowJob(threadPoolJob * job);
      void addFastJob(threadPoolJob * job);

      void flagForUpdate() {
        update++;
      }
      bool needsUpdate() { return update > 0;  }
      void updateDone() { update--; }

      // Global named bus (issue #121). Constructed lazily in init() and
      // destroyed in close() so subscribers, the SPSC queue, and the next
      // handle counter do not persist across an init/close cycle. Calling
      // namedBus() before init() or after close() is a programming error.
      NamedBus& namedBus();

      global();
      ~global();


    private:
      void init();
      void close();

      threadPool slowThreads;
      threadPool fastThreads;

      std::unique_ptr<NamedBus> bus;

      aInt update;
      aBool active; // set true after System().init(), false at System().close()
      aBool sampleRateLocked;


      friend class YSE::system; // system needs access to the init and close method
    };

    global & Global();
  }
}



#endif  // GLOBAL_H_INCLUDED
