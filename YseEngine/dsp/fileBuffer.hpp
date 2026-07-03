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

    /**
     *  @brief Drawable buffer with built-in load and save.
     *
     *  Adds file-system convenience on top of ``drawableBuffer``: load a
     *  channel from disk, or save the contents to WAV.
     */
    class API fileBuffer : public drawableBuffer {
    public:
      /** @brief Construct a file buffer. See ``buffer::buffer``. */
      fileBuffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0)
        : drawableBuffer(length, overflow) {}

      /**
       *  @brief Load one channel from an audio file.
       *
       *  @param fileName Path to the audio file.
       *  @param channel  Channel index. For mono files this must be 0.
       *  @return ``true`` on success, ``false`` if the file cannot be opened or
       *          the requested channel does not exist.
       */
      bool load(const char* fileName, UInt channel = 0);

      /**
       *  @brief Save the contents to a mono WAV file.
       *  @note WAV is currently the only supported output format.
       */
      bool save(const char* fileName);

      /** @brief Copy-assign from a ``buffer``. */
      fileBuffer& operator=(const buffer& s) {
        buffer::operator=(s);
        return *this;
      }
      /** @brief Fill every sample with ``value``. */
      fileBuffer& operator=(Flt value) {
        buffer::operator=(value);
        return *this;
      }
    };

  } // namespace DSP

} // namespace YSE

#endif // FILEBUFFER_H_INCLUDED
