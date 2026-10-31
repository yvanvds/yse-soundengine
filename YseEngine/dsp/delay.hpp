/*
  ==============================================================================

    delay.hpp
    Created: 31 Jan 2014 2:52:41pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DELAY_H_INCLUDED
#define DELAY_H_INCLUDED

#include <vector>
#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "buffer.hpp"

namespace YSE {

  namespace DSP {

    /**
     *  @brief Variable-length delay line.
     *
     *  Push audio in with ``process``, then read it back at one or more
     *  offsets with ``read``. The read offset can be a constant (fixed delay)
     *  or another buffer (per-sample modulated delay, useful for chorus,
     *  flanger, and Doppler-style effects).
     *
     *  **Memory.** The constructor allocates the line for its size at the
     *  highest sample rate it is given (``HIGHEST_SAMPLERATE`` by default).
     *  ``process`` and ``read`` allocate nothing: a size or rate that would
     *  need a longer line is clamped to that memory. Only ``reserve`` grows
     *  it (issue #1210).
     */
    class API delay {
    public:
      /** @brief The sample rate the constructor sizes the line for unless told
       *         otherwise. */
      static constexpr UInt HIGHEST_SAMPLERATE = 192000;

      /**
       *  @brief Set the line length in ms.
       *
       *  Safe from any thread; allocates nothing. Takes effect at the next
       *  ``process``, clamped to the memory reserved at construction (or by
       *  ``reserve``).
       */
      delay& setSize(UInt size);

      /**
       *  @brief Set the size and grow the memory so that size fits at
       *         @p highestSampleRate.
       *
       *  Allocates when the memory must grow; never shrinks it. Not safe to
       *  call concurrently with ``process`` or ``read``.
       */
      delay& reserve(UInt size, UInt highestSampleRate);

      /**
       *  @brief Write a block into the delay line.
       *
       *  Call once per audio processing tick. Must precede ``read`` calls for
       *  that tick. Allocates nothing.
       */
      delay& process(buffer& buffer);

      /**
       *  @brief Read from the delay at a fixed offset.
       *
       *  Allocates nothing. @p result should hold at least the last processed
       *  block; a shorter one receives only its first ``getLength()`` samples.
       *
       *  @param result    Destination buffer for the delayed audio.
       *  @param delayTime Offset in samples from the most recent write.
       */
      delay& read(buffer& result, UInt delayTime);

      /**
       *  @brief Read from the delay at a per-sample variable offset.
       *
       *  Allocates nothing; writes ``result.getLength()`` samples.
       *
       *  @param result    Destination buffer for the delayed audio.
       *  @param delayTime Per-sample offsets in samples. Use this for
       *                   modulated delays (chorus, flanger).
       */
      delay& read(buffer& result, buffer& delayTime);

      /** @brief Construct a delay line of the given initial size, with memory
       *         for that size at @p highestSampleRate. Allocates. */
      delay(Int size, UInt highestSampleRate = HIGHEST_SAMPLERATE);
      delay(const delay&);

    private:
      UInt bufferlength;
      std::vector<Flt> buffer; // sized once; the line uses its first bufferlength + 4
      Int phase;

      UInt currentLength;
      aUInt size;
    };

    /**
     *  @brief Read into ``out`` at interpolated positions ``ctrl`` from ``buffer``.
     *
     *  Free function for tabular interpolation lookups — useful when reading
     *  from a buffer at fractional sample positions.
     */
    API void readInterpolated(buffer& ctrl, buffer& out, buffer& buffer, UInt& pos);

  } // namespace DSP
} // namespace YSE

#endif // DELAY_H_INCLUDED
