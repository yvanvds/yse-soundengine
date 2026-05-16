/*
  ==============================================================================

    reverbManager.h
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBMANAGER_H_INCLUDED
#define REVERBMANAGER_H_INCLUDED

#include <mutex>
#include "reverb.hpp"
#include "reverbInterface.hpp"
#include "reverbImplementation.h"
#include "../internal/reverbDSP.h"
#include "reverbMessage.h"
#include "../internal/threadPool.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace REVERB {

    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      /** reverbManager needs extra setup because we cannot create the needed reverb objects 
          in the constructor because the forward_list reverbs might not be ready at that
          instant.
      */
      void create();

      /** Request a new implementationObject. This should be called from the interfaceObject.
      It creates an implementationObject that will be linked to the interfaceObject.

      &param head   The interface to connect the new implementation to

      @return       A pointer to a new object implementation
      */
      implementationObject * addImplementation(reverb * head);

      /** This function instructs the manager to put the implementation
      in a list of objects to load. It is called from the interfaceObject
      create function, preferably at the end when all custom work is done.

      &param impl   The implementation to setup
      */
      void setup(implementationObject * impl);

      /** Returns true if no implementations exist
      */
      Bool empty();

      /** This function calculates the effective reverb from all active reverbs within
          distance of the listener
      */
      void update();

      /** This attaches the reverb to a channel. Reverb will be applied to this channel only.
          Because applying reverb needs a lot of cpu time, this is the default way to work
          with reverb. If you really want to use more than one reverb, it can be added 
          as a post-DSPobject to a sound or a channel.
      */
      void attachToChannel(CHANNEL::implementationObject * ptr);

      /** If the reverb is attached to a channel, it will be applied here
      */
      void process(CHANNEL::implementationObject * ptr); 

      /** This function is called by the system if the number of channels changes, because
          it needs to change the reverb output channels to reflect this.
      */
      void setOutputChannels(Int value);

      /** Returns a reference to the global reverb interface. These are the settings that will
          become active if no combination of local reverbs is fully active at the current location.
      */
      reverb & getGlobalReverb();

    private:
      INTERNAL::reverbDSP reverbDSPObject; // this is the actual reverb object (there can be only one)
      CHANNEL::implementationObject * reverbChannel; // < the channel on which to apply this reverb

      reverb globalReverb;
      reverb calculatedValues;

      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Once an object is ready for use, a pointer is placed in this container. The manager will
      // update and sync all these objects during the dsp callback function
      std::forward_list<implementationObject*> inUse;

      // Lock-free SPSC inbox: main thread pushes here from setup(); audio
      // thread drains it into `toLoad` at the top of update(). Reverb impls
      // skip the slow-pool setup stage (status is set directly to
      // OBJECT_SETUP in setup()) — the inbox is purely a main→audio handoff.
      lfQueue<implementationObject*> toLoadInbox;

      // Audio-thread-owned working list of impls awaiting OBJECT_READY.
      std::forward_list<implementationObject*> toLoad;

      // Canonical list of all implementationObjects. Touched by main thread
      // (addImplementation emplace_front) and the slow-pool worker
      // (deleteJob remove_ifs). Guarded by implementationsMutex.
      std::forward_list<implementationObject> implementations;

      // Guards `implementations` between main thread and slow-pool worker.
      std::mutex implementationsMutex;

      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject & Manager();
  }
}




#endif  // REVERBMANAGER_H_INCLUDED
