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
      // Creates an audio buffer with standard size of 512 
      buffer(UInt length = STANDARD_BUFFERSIZE);
      // Creates a new audio buffer by copying an existing one
      buffer(const buffer & cp);

      // gets the length of a sample in frames (also called 'samples' like in '44100 samples per second')
      UInt  getLength() const;
      // gets the length of a sample in milliseconds
      UInt	getLengthMS() const;
      // gets the length of a sample in seconds
      Flt		getLengthSec() const;

      // WARNING: try to avoid this function. It will give you write access
      // to the internal buffer, but there might be unexpected consequenses
      Flt * getPtr();

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

      

      // get the last sample of the buffer
      Flt getBack();

      // Each buffer holds an internal cursor. You can use this to remember a
      // buffer position.
      Flt * cursor;

      // resize a sample buffer, copy current contents if needed
      // copy = true will retain the current values and fill remaining values with zeroes
      // with copy = false, the buffer values are not initialized
      buffer & resize(UInt length, Bool copy = false);

      // these functions are needed by the engine to be able to play 
      // buffers with different sample rates.
      inline Flt getSampleRateAdjustment() { return sampleRateAdjustment; }
      inline void setSampleRateAdjustment(Flt s) { sampleRateAdjustment = s; }
    
    
    protected:
      std::vector<Flt> storage;

      // to play all rates at the correct speed
      Flt sampleRateAdjustment;
    };
  }
}



#endif  // SAMPLE_H_INCLUDED
