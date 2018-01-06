/*
  ==============================================================================

    constants.h
    Created: 28 Jan 2014 2:21:52pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#include "types.hpp"

namespace YSE {
  const UInt STANDARD_BUFFERSIZE = 1024;
  const UInt STREAM_BUFFERSIZE = 44100;
  extern UInt SAMPLERATE; // this used to be a constant. It is now declared in devicemanager
}
  
  



#endif  // CONSTANTS_H_INCLUDED
