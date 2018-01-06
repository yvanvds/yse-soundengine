/*
  ==============================================================================

    reverbManager.h
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBMANAGER_H_INCLUDED
#define REVERBMANAGER_H_INCLUDED

#include "reverb.hpp"
#include "reverbInterface.hpp"
#include "reverbImplementation.h"
#include "../internal/reverbDSP.h"
#include "reverbMessage.h"
#include "../internal/threadPool.h"

namespace YSE {
  namespace REVERB {

    class managerObject {
    public:

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
          obj->implementations.remove_if(implementationObject::canBeDeleted);
        }

      private:
        managerObject * obj;
      };

      managerObject();
      ~managerObject();

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

      deleteJob mgrDelete;

      // Once an object is ready for use, a pointer is placed in this container. The manager will
      // update and sync all these objects during the dsp callback function
      std::forward_list<implementationObject*> inUse;

      // this queue is used by the setupJob. It is accessed from a low
      // priority thread to setup, but also from the dsp thread to check if an
      // object is ready. This is why every pointer has to be atomic. (It's not
      // a lot of overhead because objects are only in this container while being
      // created. Unless you create a huge amount of sounds at the same time the size
      // of this list will be small. And if you DO create a huge amount of sounds
      // at the same time you should be expecting some latency while they all get loaded
      // anyway.)
      std::forward_list<std::atomic<implementationObject*>> toLoad;

      // this is the list of all implementationObjects for this subSystem, whether they are ready, 
      // need to be setup or are about to be deleted. This list is not accessed from the 
      // audio callback thread, although elements of it might be accessed through the above pointer lists.
      std::forward_list<implementationObject> implementations;

      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      friend class deleteJob;
    };

    managerObject & Manager();
  }
}




#endif  // REVERBMANAGER_H_INCLUDED
