/*
  ==============================================================================

    channelManager.h
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELMANAGER_H_INCLUDED
#define CHANNELMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include <vector>
#include "channelImplementation.h"
#include "../headers/enums.hpp"
#include "../classes.hpp"
#include "../internalHeaders.h"
#include "../internal/threadPool.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace CHANNEL {

    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      void update();

      /** Tear down all per-session channel state so the manager can be
          re-created by a subsequent System::init(). Called from system::close()
          after both thread pools are joined and the audio device is closed,
          with Global().active already false. Clearing `implementations` runs
          each impl's destructor, which nulls its interface's pimpl — including
          the persistent master/ambient/fx/music/gui/voice channels — so
          channel::create()/createGlobal() do not trip their
          assert(pimpl == nullptr) on the next init (issue #132).
      */
      void destroy();

      implementationObject * addImplementation(channel * head);
      void setup(implementationObject * impl);
      Bool empty();
      
      // channel output configuration from interface
      void setChannelConf(CHANNEL_TYPE type, Int outputs = 2);
      
      // switch to the new configureation during audio callback
      void changeChannelConf();
      
      UInt getNumberOfOutputs();
      Flt  getOutputAngle(UInt nr);

      channel & master();
      channel & FX();
      channel & music();
      channel & ambient();
      channel & voice();
      channel & gui();

      void setMaster(implementationObject * impl);
      
    private:
      // Once an object is ready for use, a pointer is placed in this container. The manager will
      // update and sync all these objects during the dsp callback function
      std::forward_list<implementationObject*> inUse;

      INTERNAL::managerSetupJob<managerObject> mgrSetup;
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Lock-free SPSC inbox: main thread pushes here from setup(); audio
      // thread drains it into `toLoad` at the top of update().
      lfQueue<implementationObject*> toLoadInbox;

      // Audio-thread-owned working list of impls awaiting OBJECT_READY.
      std::forward_list<implementationObject*> toLoad;

      // Canonical list of all implementationObjects. Touched by main thread
      // (addImplementation emplace_front) and the slow-pool worker (setupJob
      // iterates, deleteJob remove_ifs). Guarded by implementationsMutex.
      std::forward_list<implementationObject> implementations;

      // Guards `implementations` between main thread and slow-pool worker.
      std::mutex implementationsMutex;

      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      channel _master;
      channel _fx;
      channel _music;
      channel _ambient;
      channel _voice;
      channel _gui;

      // channel output configuration
      aFlt * outputAngles;
      aUInt outputChannels;
      std::atomic<CHANNEL_TYPE> channelType;

      void setMono();
      void setStereo();
      void setQuad();
      void set51();
      void set51Side();
      void set61();
      void set71();
      void setAuto(Int count);

      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject & Manager();

  }
}



#endif  // CHANNELMANAGER_H_INCLUDED
