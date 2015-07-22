/*
  ==============================================================================

    sample_functions.h
    Created: 19 Jul 2015 8:36:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SAMPLE_FUNCTIONS_H_INCLUDED
#define SAMPLE_FUNCTIONS_H_INCLUDED

#include "sample.hpp"

namespace YSE {
  namespace DSP {
    /** Load an audio channel from file into an AUDIOBUFFER. If the file does
        not contain this channel (for example when called with value 1 on a
        mono channel file) the function will return false. If the file cannot
        be found or another error occurs while loading, the function will also
        return false.
    */
    bool API LoadFromFile(const char * fileName, AUDIOBUFFER & buffer, UInt channel = 0);
    
    /** Load all channels from file into a MULTICHANNELBUFFER. (This buffer consists
        of several AUDIOBUFFER objects.)
    */
    bool API LoadFromFile(const char * fileName, MULTICHANNELBUFFER & buffer);

    /** Save an AUDIOBUFFER to a mono audio file. Currently only the WAV
        format is supported.
    */
    bool API SaveToFile(const char * fileName, AUDIOBUFFER & buffer);
    
    /** Save multiple channels to a WAV file.
    */
    bool API SaveToFile(const char * fileName, MULTICHANNELBUFFER & buffer);
  }
}



#endif  // SAMPLE_FUNCTIONS_H_INCLUDED
