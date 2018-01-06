/*
  ==============================================================================

    fileBuffer.h
    Created: 7 Aug 2015 1:31:44pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FILEBUFFER_H_INCLUDED
#define FILEBUFFER_H_INCLUDED

#include "drawableBuffer.hpp"

namespace YSE {

  namespace DSP {

    class API fileBuffer : public drawableBuffer {
    public:
      fileBuffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0) : drawableBuffer(length, overflow) {}
      /** Load an audio channel from file. If the file does
          not contain this channel (for example when called with value 1 on a
          mono channel file) the function will return false. If the file cannot
          be found or another error occurs while loading, the function will also
          return false.
      */
      bool load(const char * fileName, UInt channel = 0);

      /** Save buffer to a mono audio file. Currently only the WAV
          format is supported.
      */
      bool save(const char * fileName);

      fileBuffer & operator=(const buffer & s) { buffer::operator=(s); return *this; }
      fileBuffer & operator=(Flt value) { buffer::operator=(value); return *this; }
    };

  }

}



#endif  // FILEBUFFER_H_INCLUDED
