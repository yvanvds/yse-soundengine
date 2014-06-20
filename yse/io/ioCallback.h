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
#include <vector>
#include "../dsp/sample.hpp"

namespace YSE {
  namespace IO {

    class ioCallback {
    public:
      virtual ~ioCallback() {}

      virtual void onCallback(const std::vector<AUDIOBUFFER> & inputChannels, std::vector<AUDIOBUFFER> & outputChannels) = 0;
      virtual void onStart() = 0;
      virtual void onStop () = 0;
      virtual void onError(const std::wstring & message) = 0;
    };

  }
}



#endif  // IOCALLBACK_H_INCLUDED
