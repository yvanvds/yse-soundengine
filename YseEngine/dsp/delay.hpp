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
     */
    class API delay {
    public:
      /**
       *  @brief Resize the delay line.
       *
       *  Longer lines use more memory but allow longer maximum delays.
       */
      delay& setSize(UInt size);

      /**
       *  @brief Write a block into the delay line.
       *
       *  Call once per audio processing tick. Must precede ``read`` calls for
       *  that tick.
       */
      delay& process(buffer& buffer);

      /**
       *  @brief Read from the delay at a fixed offset.
       *
       *  @param result    Destination buffer for the delayed audio.
       *  @param delayTime Offset in samples from the most recent write.
       */
      delay& read(buffer& result, UInt delayTime);

      /**
       *  @brief Read from the delay at a per-sample variable offset.
       *
       *  @param result    Destination buffer for the delayed audio.
       *  @param delayTime Per-sample offsets in samples. Use this for
       *                   modulated delays (chorus, flanger).
       */
      delay& read(buffer& result, buffer& delayTime);

      /** @brief Construct a delay line of the given initial size. */
      delay(Int size);
      delay(const delay&);

    private:
      UInt bufferlength;
      std::vector<Flt> buffer;
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
