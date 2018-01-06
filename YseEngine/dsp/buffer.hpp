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
    - This class serves as a basic audio buffer. It can be used for low level
    audio operations where you need access to every frame in the buffer.
    - Don't create samples as local variables because they have to
    allocate dynamic memory. This is too costly to do during callback
    functions.
    - Only the length functions are threadsafe.
    */

    class API buffer {
    public:
      // Creates an audio buffer
      buffer(UInt length = STANDARD_BUFFERSIZE, UInt overflow = 0);
      
      // Creates a new audio buffer by copying an existing one
      buffer(const buffer & cp);

      // gets the length of a sample in frames (also called 'samples' like in '44100 samples per second')
      inline UInt getLength   () const { return storage.size() - overflow; }
      // gets the length of a sample in milliseconds
      inline UInt	getLengthMS () const { return static_cast<UInt>((storage.size() - overflow) / static_cast<Flt>(SAMPLERATE * 0.001)); }
      // gets the length of a sample in seconds
      inline Flt	getLengthSec() const { return ((storage.size() - overflow) / static_cast<Flt>(SAMPLERATE)); }

      // WARNING: try to avoid this function. It will give you write access
      // to the internal buffer, but there might be unexpected consequenses
      inline Flt * getPtr() { return storage.data(); }

      // Add the same value (f) to all samples in the buffer
      buffer & operator+=(Flt f);
      // Distract the same value (f) from all samples in the buffer 
      buffer & operator-=(Flt f);
      // Multiply all samples by f
      buffer & operator*=(Flt f);
      // Divide all samples by f
      buffer & operator/=(Flt f);

      buffer & operator+=(const buffer & s);
      buffer & operator-=(const buffer & s);
      buffer & operator*=(const buffer & s);
      buffer & operator/=(const buffer & s);

      buffer & operator=(const buffer & s);
      buffer & operator=(Flt f);
      buffer & copyFrom(const buffer & s, UInt SourcePos, UInt DestPos, UInt length);

      buffer & swap(buffer & s);

      // get the last sample of the buffer
      Flt getBack();

      // Each buffer holds an internal cursor. You can use this to remember a
      // buffer position.
      Flt * cursor;

      // Resize a sample buffer. Internally, this is a std::vector resize operation.
      // If the new size is greater than the current size, new elements will be initialized
      // with value.
      buffer & resize(UInt length, Flt value = 0.f);

      // these functions are needed by the engine to be able to play 
      // buffers with different sample rates.
      inline Flt getSampleRateAdjustment() { return sampleRateAdjustment; }
      inline void setSampleRateAdjustment(Flt s) { sampleRateAdjustment = s; }
    
      // if the buffer has an overflow, this will copy the first x bytes to the end of the buffer
      // (It is unlikely that you would need this function because this happens automatically on 
      // most operations.)
      inline void copyOverflow() { for (UInt i = 0; i < overflow; i++) storage[getLength() + i] = storage[i]; }

    protected:
      std::vector<Flt> storage;

      // to play all rates at the correct speed
      Flt sampleRateAdjustment;

      // some buffers, like wavetables, use an extra value at the end which
      // contains a copy of the first value. In this case the actual storage
      // vector is the requested size + overflow
      UInt overflow;

      
    };
  }
}



#endif  // SAMPLE_H_INCLUDED
