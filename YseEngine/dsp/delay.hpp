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
    /** Delay keeps an internal delay line of variable length. You can read from a delay
        at any given position, which enables you to get a delayed buffer as a result.
    */
    class API delay {
    public:
      /** Changes the length of the delay line. Longer delay lines use more memory, 
          but allow for longer delays.

          @param size   the size of the delay line.
      */
      delay& setSize(UInt size); 

      /** Process is responsible for updating the internal delay buffer.
          It should only be called once, during audio processing.
          @param buffer   the audio buffer to store in the delay buffer.
      */
      delay& process(buffer & buffer); 

      /** Read from the delay at a fixed point and store the required
          part of the buffer in result.

          @param result     This buffer will receive the audio read from the
                            delay.

          @param delayTime  The frame at which to start reading from the
                            delay line.
      */
      delay& read(buffer& result, UInt delayTime); 

      /** Read from the delay at a variable point and store the required
          part of the buffer in result.

          @param result     This buffer will receive the audio read from the
                            delay.

          @param delayTime  A buffer containing the delay time for each frame.
      */
      delay& read(buffer & result, buffer & delayTime); // read from delay at variable point

      /** Create a delay line.

          @param size   The initial length of the delay line. This can be changed
                        afterwards, but it's faster if you provide the correct 
                        size when creating the object.
      */
      delay(Int size);
      delay(const delay &);

    private:
      UInt bufferlength;
      std::vector<Flt> buffer;
      Int phase;

      UInt currentLength; // the sample length for this loop
      aUInt size;
    };

    void readInterpolated(buffer & ctrl, buffer & out, buffer & buffer, UInt &pos);

  }
}



#endif  // DELAY_H_INCLUDED
