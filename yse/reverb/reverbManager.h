/*
  ==============================================================================

    reverbManager.h
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBMANAGER_H_INCLUDED
#define REVERBMANAGER_H_INCLUDED

#include "../internal/reverbDSP.h"
#include "JuceHeader.h"
#include "reverb.hpp"
#include "reverbImplementation.h"
#include "../templates/managerObject.h"
#include <forward_list>

namespace YSE {
  namespace REVERB {

    class managerObject : public TEMPLATE::managerObject<reverbSubSystem> {
    public:
      managerObject();
      ~managerObject();

      /** reverbManager needs extra setup because we cannot create the needed reverb objects 
          in the constructor because the forward_list reverbs might not be ready at that
          instant.
      */
      void create();

      /** This function calculates the effective reverb from all active reverbs within
          distance of the listener
      */
      void update();

      /** This attaches the reverb to a channel. Reverb will be applied to this channel only.
          Because applying reverb needs a lot of cpu time, this is the default way to work
          with reverb. If you really want to use more than one reverb, it can be added 
          as a post-DSPobject to a sound or a channel.
      */
      void attachToChannel(INTERNAL::channelImplementation * ptr);

      /** If the reverb is attached to a channel, it will be applied here
      */
      void process(INTERNAL::channelImplementation * ptr); 

      /** This function is called by the system if the number of channels changes, because
          it needs to change the reverb output channels to reflect this.
      */
      void setOutputChannels(Int value);

      /** Returns a reference to the global reverb interface. These are the settings that will
          become active if no combination of local reverbs is fully active at the current location.
      */
      reverb & getGlobalReverb();

      juce_DeclareSingleton(managerObject, true)
    private:
      
      std::forward_list<reverb *> reverbs; // these are reverb settings
      INTERNAL::reverbDSP * reverbDSPObject; // this is the actual reverb object (there can be only one)
      INTERNAL::channelImplementation * reverbChannel; // < the channel on which to apply this reverb

      reverb globalReverb;
      reverb calculatedValues;

    };

  }
}




#endif  // REVERBMANAGER_H_INCLUDED
