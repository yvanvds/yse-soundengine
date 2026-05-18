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
  const UInt STANDARD_BUFFERSIZE = 128;

  // Default streaming-chunk size in samples. Sized as ~1 s of audio at 44.1
  // kHz; at other negotiated sample rates the wall-clock duration scales
  // accordingly (~0.92 s at 48 kHz, ~0.46 s at 96 kHz). Used as a fixed
  // sample-count by file/streaming subsystems; not intended to track
  // SAMPLERATE.
  const UInt STREAM_BUFFERSIZE = 44100;

  // The active engine sample rate. Initialised to 44100 by the device
  // manager's translation unit; written exactly once per session by the
  // audio backend (PortAudio default device on desktop, Oboe-negotiated
  // rate on Android) before INTERNAL::Global().sampleRateLocked is set at
  // the end of system::initShared(). Treat as immutable within a session;
  // see the lock contract enforced in portaudioDeviceManager.cpp /
  // oboeImplementation.cpp.
  extern API UInt SAMPLERATE;
}
  
  



#endif  // CONSTANTS_H_INCLUDED
