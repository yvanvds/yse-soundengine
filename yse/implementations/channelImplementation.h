/*
  ==============================================================================

    channelImplementation.h
    Created: 30 Jan 2014 4:21:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELIMPLEMENTATION_H_INCLUDED
#define CHANNELIMPLEMENTATION_H_INCLUDED
#include "../headers/types.hpp"
#include "JuceHeader.h"
#include "../dsp/sample.hpp"
#include "../internal/reverbDSP.h"
#include "../classes.hpp"
#include <forward_list>

namespace YSE {
  namespace INTERNAL {
    class output;

    /**
      This is the implementation side of a channel. It should only be used internally.
    */
    class channelImplementation : public ThreadPoolJob {
    public:
      /**
        Creates a channel implementation.

        @param name   The name of the channel. This can be used in logfiles.
        @param head   A pointer to the interface of this channel.
      */
      channelImplementation(const String & name, channel * head);

      /**
        Removes the implementation from the threadpool and moves all sounds and subchannels
        to its parent (if there is one).
      */
      ~channelImplementation();

      /**
        Attach a subchannel to this channel.
        @param subChannel   A pointer to the channel that should be linked
                            to this channel.
      */
      Bool connect(channelImplementation * channel);
      
      /**
        Attach a sound to this channel.
        @param sound      A pointer to the sound that should be linked
                          to this channel.
      */
      Bool connect(soundImplementation   * sound);
      
      /**
        Remove a subchannel from this channel.
        @param channel    A pointer to the channel to disconnect
      */
      Bool disconnect(channelImplementation * channel);

      /**
        Remove a sound from this channel.
        @param sound      A pointer to the sound to disconnect
      */
      Bool disconnect(soundImplementation   * sound);

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

    private:

      /** This function is called from channelManager::setup and creates the buffers
          needed for this channel.
      */
      void setup();

      /** This function is called by channelManager::update (from dsp callback) and verifies
          if the channel is ready to be played. It will then be moved from toCreate
          to inUse.
      */
      Bool readyCheck();

      /** This function is called by channelManager::update (from dsp callback) to 
          syncronize this channel with it's interface
      */
      void sync();

      /** The exit function waits for the current job to finish (if there is one) before 
          returning. It is called by the destructor.
      */
      void exit();

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

      channelImplementation * parent;

      std::forward_list<channelImplementation*> children;
      std::forward_list<soundImplementation *> sounds;

      std::vector<output> outConf;
      std::vector<DSP::sample> out;

      Bool userChannel; // channel is created by user and not crucial for the system
      Bool allowVirtual;

      channel * head;
      std::atomic<CHANNEL_IMPLEMENTATION_STATE> objectStatus;

      friend class soundImplementation;
      friend class channel;
      friend class reverbManager;
      friend class deviceManager;
      friend class channelManager;
      friend class soundManager;
      friend class channelSetupJob;
      friend class channelDeleteJob;
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
