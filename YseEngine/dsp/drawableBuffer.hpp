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

    class API drawableBuffer : public buffer {
    public:
      drawableBuffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0) : buffer(length, overflow) {}

      /** Apply an envelope to the current audiobuffer.
      @param length       Desired length of the envelope in seconds. If length > 0, the
      envelope will be scaled to this length. Otherwise the internal
      length of the envelope will be used.
      */
      drawableBuffer & applyEnvelope(const envelope & env, Flt length = 0);

      /** Draw data in a sound buffer. This is not meant for buffers
      which will be sent to the audio output, but for buffers used to do
      calculations on real audio buffers. Make sure that start and stop values
      are within the bounds of this buffer (0 -> getLength()). If the startValue
      differs from the stopValue, values inbetween will be created as a linear slope.

      @param start        the position in the buffer you want to start drawing
      @param stop         the position in the buffer you want to stop drawing
      @param startValue   the value you want to start with. (Between -1 and 1)
      @param stopValue    the value you want to stop at. (Between -1 and 1)
      */
      drawableBuffer & drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue);

      /** Fill (part of) a buffer with one value. This is not meant for buffers
      which will be sent to the audio output, but for buffers used to do
      calculations on real audio buffers. Make sure that start and stop values
      are within the bounds of this buffer (0 -> getLength()). In a visual
      representation the result is an horizontal line.

      @param start        the position in the buffer you want to start drawing
      @param stop         the position in the buffer you want to stop drawing
      @param value        the value to set. (Between -1 and 1)
      */
      drawableBuffer & drawLine(UInt start, UInt stop, Flt value); // horizontal line

      drawableBuffer & operator+=(Flt f) { buffer::operator+=(f); return *this; }
      drawableBuffer & operator-=(Flt f) { buffer::operator-=(f); return *this; }
      drawableBuffer & operator*=(Flt f) { buffer::operator*=(f); return *this; }
      drawableBuffer & operator/=(Flt f) { buffer::operator/=(f); return *this; }

      drawableBuffer & operator+=(const buffer & s) { buffer::operator+=(s); return *this; }
      drawableBuffer & operator-=(const buffer & s) { buffer::operator-=(s); return *this; }
      drawableBuffer & operator*=(const buffer & s) { buffer::operator*=(s); return *this; }
      drawableBuffer & operator/=(const buffer & s) { buffer::operator/=(s); return *this; }

      drawableBuffer & operator=(const buffer & s) { buffer::operator=(s); return *this; }
      drawableBuffer & operator=(Flt f) { buffer::operator=(f); return *this; }
    };

  }
}



#endif  // DRAWABLEBUFFER_H_INCLUDED
