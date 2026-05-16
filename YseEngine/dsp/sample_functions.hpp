/*
  ==============================================================================

    sample_functions.h
    Created: 19 Jul 2015 8:36:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SAMPLE_FUNCTIONS_H_INCLUDED
#define SAMPLE_FUNCTIONS_H_INCLUDED

#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Load a single channel from an audio file into a buffer.
     *
     *  @param fileName Path to the audio file.
     *  @param buffer   Destination buffer. Resized to fit.
     *  @param channel  Channel index to load. For mono files this must be 0.
     *  @return ``true`` on success, ``false`` if the file cannot be opened or
     *          the requested channel does not exist.
     */
    bool API LoadFromFile(const char * fileName, buffer & buffer, UInt channel = 0);

    /**
     *  @brief Load every channel of an audio file into a multichannel buffer.
     *
     *  @return ``true`` on success, ``false`` on read error.
     */
    bool API LoadFromFile(const char * fileName, MULTICHANNELBUFFER & buffer);

    /**
     *  @brief Save a single-channel buffer to a mono WAV file.
     *
     *  @note WAV is currently the only supported output format.
     *  @return ``true`` on success, ``false`` on write error.
     */
    bool API SaveToFile(const char * fileName, buffer & buffer);

    /** @brief Save a multichannel buffer to a WAV file. */
    bool API SaveToFile(const char * fileName, MULTICHANNELBUFFER & buffer);

    /** @brief Scale ``buffer`` so its peak absolute sample value equals 1.0. */
    void API Normalize(buffer & buffer);
  }
}



#endif  // SAMPLE_FUNCTIONS_H_INCLUDED
