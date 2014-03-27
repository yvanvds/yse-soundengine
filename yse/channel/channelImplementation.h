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
#include "JuceHeader.h"
#include "../templates/implementationObject.h"
#include "channel.hpp"
#include "../sound/soundImplementation.h"
#include "channelInterface.hpp"
#include "channelManager.h"

namespace YSE {
  namespace CHANNEL {
    class output;

    /**
      This is the implementation side of a channel. It should only be used internally.
    */
    class implementationObject : public ThreadPoolJob , public TEMPLATE::implementationTemplate<channelSubSystem> {
    public:
      /**
        Creates a channel implementation.

        @param name   The name of the channel. This can be used in logfiles.
        @param head   A pointer to the interface of this channel.
      */
      implementationObject(interfaceObject * head);

      virtual void parseMessage(const messageObject & message); // < Parse all messages, if any

      /**
        Removes the implementation from the threadpool and moves all sounds and subchannels
        to its parent (if there is one).
      */
      virtual void exit();

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

      /**
        This is the threadpool function that calls the dsp for this channel.
        Every channel has its own threadPoolJob for running the dsp calculations.
        This will scale all sounds nicely over several cpu's as long as you don't
        put them all in one channel.
      */
      JobStatus runJob(); 

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

      /**
        Attach the premade special effect for 'underwater' simulation to this channel
      */
      void attachUnderWaterFX();

      virtual void doThisWhenReady();

    private:

      /** This function is called from channelManager::setup and creates the buffers
          needed for this channel.
      */
      void implementationSetup();

      /** This function is called by channelManager::update (from dsp callback) and verifies
          if the channel is ready to be played. It will then be moved from toCreate
          to inUse.
      */
      virtual Bool implementationReadyCheck();

      /** Will move all current subchannels and sounds to the parent channel.
          This function is called from channelManager::update(), when objectStatus
          is CIS_RELEASE. It is called again from the destructor, just in case, but 
          children should already be moved by then.
      */
      void childrenToParent();
      
      /** dsp utility function to set all output buffers to zero before filling again
      */
      void clearBuffers();

      /** Creates a ramp to the new volume if needed, to avoid pops. Called by the dsp function.
      */
      void adjustVolume();


      Flt  newVolume;
      Flt  lastVolume;

      CHANNEL::implementationObject * parent;

      std::forward_list<CHANNEL::implementationObject*> children;
      std::forward_list<SOUND::implementationObject *> sounds;

      std::vector<output> outConf;
      std::vector<DSP::sample> out;

      Bool userChannel; // channel is created by user and not crucial for the system
      Bool allowVirtual;

      friend class SOUND::implementationObject;
      friend class CHANNEL::interfaceObject;
      friend class YSE::REVERB::managerObject;
      friend class INTERNAL::deviceManager;
      friend class CHANNEL::managerObject;
      friend class SOUND::managerObject;
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
