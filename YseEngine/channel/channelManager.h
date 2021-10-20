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
#include "channelImplementation.h"
#include "../headers/enums.hpp"
#include "../classes.hpp"
#include "../internalHeaders.h"
#include "../internal/threadPool.h"

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
        (Which is called the setup function of the objects that need to be loaded)
        */
        virtual void run() {
          for (auto i = obj->toLoad.begin(); i != obj->toLoad.end(); ++i) {
            i->load()->setup();
          }
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
