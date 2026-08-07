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
       *
       *  @param fileName Destination path, used verbatim — no extension is
       *                  appended, so pass the ".wav" yourself.
       *  @return ``true`` when every sample was written, ``false`` if the file
       *          cannot be created, the buffer is empty, or a custom IO backend
       *          is active (``IO().getActive()``, which is read-only).
       *
       *  @note WAV is currently the only supported output format. Samples are
       *        stored as 32-bit float, at the sample rate of the file this
       *        buffer was loaded from, or the engine rate if it was not loaded
       *        from one.
       *  @note File I/O: never call this from the audio callback.
       */
      bool save(const char* fileName);

      /**
       *  @brief Native sample rate of the last loaded file, in Hz (0 before a
       *         successful ``load``).
       *
       *  Unlike ``getSampleRateAdjustment`` — a ratio against the SAMPLERATE
       *  in effect at load time — this is rate-independent, so consumers can
       *  re-derive the playback-speed adjustment against the *live* engine
       *  rate after a ``system::close()`` / ``init()`` cycle (issue #637).
       */
      Flt getFileSampleRate() const {
        return fileRate;
      }

      // Covariant-return wrappers over the (non-virtual) base assignment
      // operators, same idiom as drawableBuffer -- see the note there.
      // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)

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
      // NOLINTEND(bugprone-derived-method-shadowing-base-method)

    private:
      Flt fileRate = 0.0f; // native rate of the loaded file, Hz (issue #637)
    };

  } // namespace DSP

} // namespace YSE

#endif // FILEBUFFER_H_INCLUDED
