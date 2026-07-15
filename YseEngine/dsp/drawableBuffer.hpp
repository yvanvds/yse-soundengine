/*
  ==============================================================================

    drawableBuffer.h
    Created: 21 Jul 2015 9:26:18pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DRAWABLEBUFFER_H_INCLUDED
#define DRAWABLEBUFFER_H_INCLUDED

#include "buffer.hpp"

namespace YSE {

  namespace DSP {
    class API envelope;

    /**
     *  @brief Buffer that can be shaped procedurally with envelopes and lines.
     *
     *  Extends ``buffer`` with drawing primitives — useful for shaping control
     *  signals, building wavetables, or constructing test signals offline. The
     *  drawing primitives target buffers used as inputs to other DSP, not
     *  buffers fed straight to the audio device, although the engine does not
     *  prevent the latter.
     */
    class API drawableBuffer : public buffer {
    public:
      /** @brief Construct a drawable buffer. See ``buffer::buffer``. */
      drawableBuffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0)
        : buffer(length, overflow) {}

      /**
       *  @brief Multiply the buffer by an envelope shape.
       *
       *  @param env    Envelope to apply.
       *  @param length Desired envelope length in seconds. If > 0 the envelope
       *                is stretched or compressed to this length; if 0 the
       *                envelope's internal length is used as-is.
       */
      drawableBuffer& applyEnvelope(const envelope& env, Flt length = 0);

      /**
       *  @brief Draw a linear ramp between two positions.
       *
       *  Writes a straight line from ``(start, startValue)`` to
       *  ``(stop, stopValue)`` into the buffer.
       *
       *  @param start      First sample index (in [0, getLength()]).
       *  @param stop       Last sample index (in [0, getLength()]).
       *  @param startValue Value at ``start``, in [-1.0, 1.0].
       *  @param stopValue  Value at ``stop``, in [-1.0, 1.0].
       */
      drawableBuffer& drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue);

      /**
       *  @brief Fill a region with a constant value.
       *
       *  @param start First sample index.
       *  @param stop  Last sample index.
       *  @param value Value to write, in [-1.0, 1.0].
       */
      drawableBuffer& drawLine(UInt start, UInt stop, Flt value);

      /** @brief Add ``f`` to every sample (returns drawableBuffer&). */
      drawableBuffer& operator+=(Flt f) {
        buffer::operator+=(f);
        return *this;
      }
      /** @brief Subtract ``f`` from every sample. */
      drawableBuffer& operator-=(Flt f) {
        buffer::operator-=(f);
        return *this;
      }
      /** @brief Multiply every sample by ``f``. */
      drawableBuffer& operator*=(Flt f) {
        buffer::operator*=(f);
        return *this;
      }
      /** @brief Divide every sample by ``f``. */
      drawableBuffer& operator/=(Flt f) {
        buffer::operator/=(f);
        return *this;
      }

      /** @brief Sample-wise add. */
      drawableBuffer& operator+=(const buffer& s) {
        buffer::operator+=(s);
        return *this;
      }
      /** @brief Sample-wise subtract. */
      drawableBuffer& operator-=(const buffer& s) {
        buffer::operator-=(s);
        return *this;
      }
      /** @brief Sample-wise multiply. */
      drawableBuffer& operator*=(const buffer& s) {
        buffer::operator*=(s);
        return *this;
      }
      /** @brief Sample-wise divide. */
      drawableBuffer& operator/=(const buffer& s) {
        buffer::operator/=(s);
        return *this;
      }

      /** @brief Copy-assign from a ``buffer``. */
      drawableBuffer& operator=(const buffer& s) {
        buffer::operator=(s);
        return *this;
      }
      /** @brief Fill every sample with ``f``. */
      drawableBuffer& operator=(Flt f) {
        buffer::operator=(f);
        return *this;
      }
    };

  } // namespace DSP
} // namespace YSE

#endif // DRAWABLEBUFFER_H_INCLUDED
