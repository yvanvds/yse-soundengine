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
#include "../classes.hpp"

namespace YSE {
  namespace INTERNAL {


    class reverbManager {
    public:
      reverbManager();
      ~reverbManager();

      void add(reverb * r);
      void remove(reverb * r);

      void attachToChannel(INTERNAL::channelImplementation * ptr);
      void process(INTERNAL::channelImplementation * ptr); // will only process if the channel is attached to the reverb

      juce_DeclareSingleton(reverbManager, true)
    private:
      Array<reverb *, CriticalSection> reverbs; // these are reverb settings
      reverbDSP * reverbObject; // this is the actual reverb object (there can be only one)
      INTERNAL::channelImplementation * reverbChannel;
      CriticalSection reverbDSPLock;
    };

  }
}




#endif  // REVERBMANAGER_H_INCLUDED
