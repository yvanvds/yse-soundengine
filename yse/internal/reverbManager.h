/*
  ==============================================================================

    reverbManager.h
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBMANAGER_H_INCLUDED
#define REVERBMANAGER_H_INCLUDED

#include "reverbDSP.h"
#include "JuceHeader.h"
#include "../reverb.hpp"
#include "manager.h"
#include <forward_list>

namespace YSE {
  namespace INTERNAL {
    class reverbImplementation;

    class reverbManager : public manager<reverbImplementation, reverb> {
    public:
      reverbManager();
      ~reverbManager();

      /** reverbManager needs extra setup because we cannot create reverb objects 
      */
      void create();

      void add(reverb * r);
      void remove(reverb * r);

      void update();

      void attachToChannel(INTERNAL::channelImplementation * ptr);
      void process(INTERNAL::channelImplementation * ptr); // will only process if the channel is attached to the reverb

      void setOutputChannels(Int value);
      reverb & getGlobalReverb();

      juce_DeclareSingleton(reverbManager, true)
    private:
      
      std::forward_list<reverb *> reverbs; // these are reverb settings
      reverbDSP * reverbObject; // this is the actual reverb object (there can be only one)
      INTERNAL::channelImplementation * reverbChannel;

      // pointers because we have to construct them after the forward list is ready
      reverb globalReverb;
      reverb calculatedValues;

    };

  }
}




#endif  // REVERBMANAGER_H_INCLUDED
