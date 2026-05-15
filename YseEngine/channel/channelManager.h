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
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace CHANNEL {

    class managerObject {
    public:

      /////////////////////////////////////////////////////////
      // setupJob
      /////////////////////////////////////////////////////////
      
      /** A job to add to the lowpriority threadpool when there are implementationObjects
      that need to be setup.
      */
      class setupJob : public INTERNAL::threadPoolJob {
      public:

        /** The job will be initialized with a name for debug purposes and a pointer
        to the managerObject it is supposed to work with. The managerObject takes
        care of adding this job to a low priority threadpool every time it sees
        that there are implementationObjects that need to be set up.
        */
        setupJob(managerObject * obj)
          : obj(obj) {

        }

        /** This function is called from the threadpool and does the intented work
        (Which is called the setup function of the objects that need to be loaded).

        Iterates the canonical `implementations` list under `implementationsMutex`
        and CAS-claims each impl whose status is OBJECT_CREATED. The mutex is
        released before calling setup() so the main thread is not blocked.
        */
        virtual void run() {
          std::vector<implementationObject*> pending;
          {
            std::lock_guard<std::mutex> lk(obj->implementationsMutex);
            for (auto & impl : obj->implementations) {
              if (impl.tryClaimForSetup()) {
                pending.push_back(&impl);
              }
            }
          }
          for (auto * p : pending) p->setup();
        }

      private:
        managerObject * obj;
      };

      /////////////////////////////////////////////////////////
      // deleteJob
      /////////////////////////////////////////////////////////
      
      /** A job to add to the lowpriority threadpool when there are implementationObjects
      to be deleted
      */
      class deleteJob : public INTERNAL::threadPoolJob {
      public:

        /** The job will be initialized with a name for debug purposes and a pointer
        to the managerObject it is supposed to work with. The managerObject takes
        care of adding this job to a low priority threadpool every time it sees
        that there are implementationObjects that need to be deleted.
        */
        deleteJob(managerObject * obj)
          : obj(obj) {

        }

        virtual void run() {
          std::lock_guard<std::mutex> lk(obj->implementationsMutex);
          obj->implementations.remove_if(implementationObject::canBeDeleted);
        }

      private:
        managerObject * obj;
      };

      /////////////////////////////////////////////////////////
      // managerObject
      /////////////////////////////////////////////////////////
      
      managerObject();
      ~managerObject();

      void update();

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

      setupJob mgrSetup;
      deleteJob mgrDelete;

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

      friend class setupJob;
      friend class deleteJob;
    };

    managerObject & Manager();

  }
}



#endif  // CHANNELMANAGER_H_INCLUDED
