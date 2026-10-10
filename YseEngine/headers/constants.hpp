/*
  ==============================================================================

    constants.h
    Created: 28 Jan 2014 2:21:52pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

/** @file
 *  @brief Engine-wide constants: the block size and the active sample rate.
 */

#include "types.hpp"

namespace YSE {
  /** @brief Samples per audio block. The engine renders, and advances its
   *         clocks, in blocks of this size whatever the device buffer size. */
  const UInt STANDARD_BUFFERSIZE = 128;

  /** @brief Streaming chunk size in samples.
   *
   *  About 1 s of audio at 44.1 kHz; at other sample rates the wall-clock
   *  length scales (about 0.92 s at 48 kHz, 0.46 s at 96 kHz). A fixed sample
   *  count used by the file and streaming code; it does not follow
   *  ``SAMPLERATE``. */
  const UInt STREAM_BUFFERSIZE = 44100;

  /** @brief The active engine sample rate, in Hz.
   *
   *  48000 until a session starts. The audio backend writes it once per
   *  session, while the session starts: the rate the application requested
   *  (``System().requestSampleRate``) or the device default on desktop, the
   *  rate Oboe negotiated on Android. Read it, never write it, and treat it as
   *  fixed for the rest of the session. */
  extern API UInt SAMPLERATE;
} // namespace YSE

#endif // CONSTANTS_H_INCLUDED
