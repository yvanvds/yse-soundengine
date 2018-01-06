/*
  ==============================================================================

    channelImplementation.h
    Created: 30 Jan 2014 4:21:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELIMPLEMENTATION_H_INCLUDED
#define CHANNELIMPLEMENTATION_H_INCLUDED

#include <forward_list>
#include "../classes.hpp"
#include "../utils/lfQueue.hpp"
#include "../internal/threadPool.h"

namespace YSE {
  namespace CHANNEL {
    class output;

    /**
      This is the implementation side of a channel. It should only be used internally.
    */
    class implementationObject : public INTERNAL::threadPoolJob {
    public:

      //////////////////////////////////////////////////
      // Setup and maintenance functions
      //////////////////////////////////////////////////
      
      /**
        Creates a channel implementation.

        @param head   A pointer to the interface of this channel.
      */
      implementationObject(channel * head);

      /**
      Removes the implementation from the threadpool and moves all sounds and subchannels
      to its parent (if there is one).
      */
      ~implementationObject();

      /** This function is called from channelManager::setup and creates the buffers
      needed for this channel.
      */
      void setup();

      /** This function resizes some containers to the number of output channels used
          by the current device.

          @param deep If true, children will also be resized

      */
      void resize(bool deep = false);


      /** This function is called by channelManager::update (from dsp callback) and verifies
      if the channel is ready to be played. It will then be moved from toCreate
      to inUse.
      */
      Bool readyCheck();

      /** Will move all current subchannels and sounds to the parent channel.
      This function is called from channelManager::update(), when objectStatus
      is CIS_RELEASE. It is called again from the destructor, just in case, but
      children should already be moved by then.
      */
      void childrenToParent();

      void removeInterface();
      void doThisWhenReady();

      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      //////////////////////////////////////////////////////
      // Message system functions
      //////////////////////////////////////////////////////
      /**
      This function will be called when the system is instructed to do an update. It looks
      for messages in the interface message pipe and forwards them to the parseMessage
      function.
      */
      void sync();
      void parseMessage(const messageObject & message); // < Parse a message, called by sync
      
      /**
        Called by an interface object to send a message to the implementation.
      */
      inline void sendMessage(const messageObject & message) {
        messages.push(message);
      }

      //////////////////////////////////////////////////////
      // Connectors
      //////////////////////////////////////////////////////
      /**
        Attach a subchannel to this channel.
        @param subChannel   A pointer to the channel that should be linked
                            to this channel.
      */
      Bool connect(CHANNEL::implementationObject * channel);
      
      /**
        Attach a sound to this channel.
        @param sound      A pointer to the sound that should be linked
                          to this channel.
      */
      Bool connect(SOUND::implementationObject * sound);
      
      /**
        Remove a subchannel from this channel.
        @param channel    A pointer to the channel to disconnect
      */
      Bool disconnect(CHANNEL::implementationObject * channel);

      /**
        Remove a sound from this channel.
        @param sound      A pointer to the sound to disconnect
      */
      Bool disconnect(SOUND::implementationObject * sound);

      /////////////////////////////////////////////////////
      // DSP calculations
      /////////////////////////////////////////////////////
      /**
        This is the threadpool function that calls the dsp for this channel.
        Every channel has its own threadPoolJob for running the dsp calculations.
        This will scale all sounds nicely over several cpu's as long as you don't
        put them all in one channel.
      */
      virtual void run(); 

      /**
        This is the one that does all the work. It allso calls the dsp function
        of all child channels and of all sounds. Effects are also calculated
        here, but applied in the buffersToParent function.
      */
      void dsp();

      /**
        Waits until the dsp job is done and recursively calls this function for 
        all subchannels. If this is not the Master channel, this will copy the
        current buffers to the parent channel.
      */
      void buffersToParent();

      /** dsp utility function to set all output buffers to zero before filling again
      */
      void clearBuffers();

      /** Creates a ramp to the new volume if needed, to avoid pops. Called by the dsp function.
      */
      void adjustVolume();


      /**
        Attach the premade special effect for 'underwater' simulation to this channel
      */

      void attachUnderWaterFX();

      inline std::vector<DSP::buffer> & GetBuffers() { return out; }
      
      /////////////////////////////////////////////////////
      // static functions for remove_if
      /////////////////////////////////////////////////////
      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus == OBJECT_DELETE;
      }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeRemovedFromLoading(const std::atomic<implementationObject*> & impl) {
        if (impl.load()->objectStatus == OBJECT_READY
          || impl.load()->objectStatus == OBJECT_RELEASE
          || impl.load()->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

    private:
      std::atomic<channel *> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<messageObject> messages;

      Flt  newVolume;
      Flt  lastVolume;

      CHANNEL::implementationObject * parent;

      std::forward_list<CHANNEL::implementationObject*> children;
      std::forward_list<SOUND::implementationObject *> sounds;

      std::vector<output> outConf;
      std::vector<DSP::buffer> out;

      Bool userChannel; // channel is created by user and not crucial for the system
      Bool allowVirtual;

      friend class SOUND::implementationObject;
      friend class YSE::channel;
      friend class YSE::REVERB::managerObject;
      friend class DEVICE::managerObject;
      friend class DEVICE::deviceManager;
      friend class CHANNEL::managerObject;
    };


    /**
    This is a helper class for calculating the channel and sound volume. Don't use it
    anywhere else.
    */
    class output {
    public:
      Flt angle;
      Flt initPan;
      Flt initGain;
      Flt effective;
      Flt ratio;
      Flt finalGain;

      output() : angle(0.f) {}
    };

  }
}




#endif  // CHANNELIMPLEMENTATION_H_INCLUDED
