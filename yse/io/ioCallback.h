/*
  ==============================================================================

    ioCallback.h
    Created: 12 Jun 2014 6:29:20pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IOCALLBACK_H_INCLUDED
#define IOCALLBACK_H_INCLUDED

#include <string>

namespace YSE {
  namespace IO {
    class ioDevice;

    class ioCallback {
    public:
      virtual ~ioCallback() {}

      virtual void onCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) = 0;
      virtual void onStart(ioDevice * device) = 0;
      virtual void onStop() = 0;
      virtual void onError(const std::string & message) {}
    };

  }
}



#endif  // IOCALLBACK_H_INCLUDED
