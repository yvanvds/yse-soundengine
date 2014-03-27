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
    /*
    - This class serves as a sound buffer. It can be used for low level
    audio operations where you need access to every frame in the buffer.
    - Don't create samples as local variables because they have to
    allocate dynamic memory. This is too costly to do during callback
    functions.
    - Don't use sample outside DSP routines if you can avoid it.
    - Only the length functions are threadsafe.
    */
    class API sample {
    public:
      // Creates an audio buffer with standard size of 512 
      sample(UInt length = STANDARD_BUFFERSIZE);
      // Creates a new audio buffer by copying an existing one
      sample(const AUDIOBUFFER & cp);

      // gets the length of a sample in frames (also called 'samples' like in '44100 samples per second')
      UInt  getLength() const;
      // gets the length of a sample in milliseconds
      UInt	getLengthMS() const;
      // gets the length of a sample in seconds
      Flt		getLengthSec() const;

      // WARNING: try to avoid this function. It will give you write access
      // to the internal buffer, but there might be unexpected consequenses
      Flt * getBuffer();

      // Add the same value (f) to all samples in the buffer
      AUDIOBUFFER & operator+=(Flt f);
      // Distract the same value (f) from all samples in the buffer 
      AUDIOBUFFER & operator-=(Flt f);
      // Multiply all samples by f
      AUDIOBUFFER & operator*=(Flt f);
      // Divide all samples by f
      AUDIOBUFFER & operator/=(Flt f);

      AUDIOBUFFER & operator+=(const AUDIOBUFFER & s);
      AUDIOBUFFER & operator-=(const AUDIOBUFFER & s);
      AUDIOBUFFER & operator*=(const AUDIOBUFFER & s);
      AUDIOBUFFER & operator/=(const AUDIOBUFFER & s);

      AUDIOBUFFER & operator=(const AUDIOBUFFER & s);
      AUDIOBUFFER & operator=(Flt f);
      AUDIOBUFFER & copyFrom(const AUDIOBUFFER & s, UInt SourcePos, UInt DestPos, UInt length);

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
      sample& drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue);

      /** Fill (part of) a buffer with one value. This is not meant for buffers
      which will be sent to the audio output, but for buffers used to do
      calculations on real audio buffers. Make sure that start and stop values
      are within the bounds of this buffer (0 -> getLength()). In a visual
      representation the result is an horizontal line.

      @param start        the position in the buffer you want to start drawing
      @param stop         the position in the buffer you want to stop drawing
      @param value        the value to set. (Between -1 and 1)
      */
      sample& drawLine(UInt start, UInt stop, Flt value); // horizontal line

      // get the last sample of the buffer
      Flt getBack();

      // Each buffer holds an internal cursor. You can use this to remember a
      // buffer position.
      Flt * cursor;

      // resize a sample buffer, copy current contents if needed
      // copy = true will retain the current values and fill remaining values with zeroes
      // with copy = false, the buffer values are not initialized
      AUDIOBUFFER & resize(UInt length, Bool copy = false);
    private:
      std::vector<Flt> buffer;
    };
  }
}



#endif  // SAMPLE_H_INCLUDED
