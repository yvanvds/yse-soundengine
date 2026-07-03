/*
  ==============================================================================

    sample.h
    Created: 28 Jan 2014 2:25:46pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SAMPLE_H_INCLUDED
#define SAMPLE_H_INCLUDED

#include <vector>
#include "../headers/constants.hpp"

namespace YSE {

  namespace DSP {

    /**
     *  @brief Single-channel float audio buffer.
     *
     *  The fundamental container for sample-level audio data in libYSE.
     *  Sub-classes ``drawableBuffer``, ``fileBuffer``, and ``wavetable`` layer
     *  drawing, file I/O, and wavetable generation on top of this base.
     *
     *  @warning **Do not construct ``buffer`` instances as audio-thread locals.**
     *           Construction allocates from the heap, which is far too slow
     *           for a real-time callback. Allocate buffers at file scope or
     *           during setup and reuse them.
     *
     *  @note Only the ``getLength*`` accessors are thread-safe; all mutating
     *        operations and sample-level reads assume the caller has
     *        serialised access.
     */
    class API buffer {
    public:
      /** @brief Construct a buffer.
       *
       *  @param length   Number of samples in the buffer.
       *  @param overflow Extra samples appended past the end, used by sources
       *                  that need a wrap-around copy of the first sample at
       *                  the tail (wavetables). Leave at 0 for plain buffers.
       */
      buffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0);

      /** @brief Copy-construct from another buffer. */
      buffer(const buffer& cp);

      /** @brief Length in samples (frames). */
      inline UInt getLength() const {
        return (UInt)storage.size() - overflow;
      }

      /** @brief Length in milliseconds at the engine sample rate. */
      inline UInt getLengthMS() const {
        return static_cast<UInt>((storage.size() - overflow) /
                                 static_cast<Flt>(SAMPLERATE * 0.001));
      }

      /** @brief Length in seconds at the engine sample rate. */
      inline Flt getLengthSec() const {
        return ((storage.size() - overflow) / static_cast<Flt>(SAMPLERATE));
      }

      /** @brief Whether every sample in the buffer is zero. */
      bool isSilent() const;

      /** @brief Peak absolute sample value. */
      float maxValue() const;

      /** @brief Direct write access to the underlying storage.
       *
       *  @warning Bypasses every internal invariant maintained by the buffer.
       *           Prefer the operator overloads, ``copyFrom``, or the
       *           ``drawableBuffer`` interface where possible.
       */
      inline Flt* getPtr() {
        return storage.data();
      }

      /** @brief Add ``f`` to every sample. */
      buffer& operator+=(Flt f);

      /** @brief Subtract ``f`` from every sample. */
      buffer& operator-=(Flt f);

      /** @brief Multiply every sample by ``f``. */
      buffer& operator*=(Flt f);

      /** @brief Divide every sample by ``f``. */
      buffer& operator/=(Flt f);

      /** @brief Sample-wise add ``s`` into this buffer. */
      buffer& operator+=(const buffer& s);

      /** @brief Sample-wise subtract ``s`` from this buffer. */
      buffer& operator-=(const buffer& s);

      /** @brief Sample-wise multiply this buffer by ``s``. */
      buffer& operator*=(const buffer& s);

      /** @brief Sample-wise divide this buffer by ``s``. */
      buffer& operator/=(const buffer& s);

      /** @brief Copy-assign from another buffer. */
      buffer& operator=(const buffer& s);

      /** @brief Fill every sample with ``f``. */
      buffer& operator=(Flt f);

      /** @brief Copy a region from ``s`` into this buffer.
       *
       *  @param s         Source buffer.
       *  @param SourcePos First sample read from ``s``.
       *  @param DestPos   First sample written in this buffer.
       *  @param length    Number of samples to copy.
       */
      buffer& copyFrom(const buffer& s, UInt SourcePos, UInt DestPos, UInt length);

      /** @brief Swap contents with another buffer in O(1). */
      buffer& swap(buffer& s);

      /** @brief Last sample of the buffer. */
      Flt getBack();

      /** @brief Position cursor — application-owned read/write head pointer.
       *
       *  The buffer never mutates this on its own; it's a parking slot for
       *  user code that needs to remember a position across calls.
       */
      Flt* cursor;

      /** @brief Resize the buffer.
       *
       *  @param length Target length in samples.
       *  @param value  Value used to initialise newly added samples when
       *                ``length`` exceeds the current size.
       */
      buffer& resize(UInt length, Flt value = 0.f);

      /** @brief Sample-rate adjustment factor used by the engine to play this
       *         buffer at the correct speed when its native rate differs from
       *         the engine rate.
       */
      inline Flt getSampleRateAdjustment() {
        return sampleRateAdjustment;
      }

      /** @brief Set the sample-rate adjustment factor. Normally only the engine calls this. */
      inline void setSampleRateAdjustment(Flt s) {
        sampleRateAdjustment = s;
      }

      /** @brief Refresh the overflow tail (a copy of the first ``overflow`` samples).
       *
       *  Most mutating operations already maintain this; calling it manually is
       *  rarely necessary.
       */
      inline void copyOverflow() {
        for (UInt i = 0; i < overflow; i++)
          storage[getLength() + i] = storage[i];
      }

    protected:
      std::vector<Flt> storage;

      Flt sampleRateAdjustment;

      UInt overflow;
    };
  } // namespace DSP
} // namespace YSE

#endif // SAMPLE_H_INCLUDED
