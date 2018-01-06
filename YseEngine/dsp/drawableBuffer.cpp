/*
  ==============================================================================

    drawableBuffer.cpp
    Created: 21 Jul 2015 9:26:18pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include "drawableBuffer.hpp"
#include "envelope.hpp"

namespace YSE {
  namespace DSP {

    drawableBuffer & YSE::DSP::drawableBuffer::applyEnvelope(const envelope & env, Flt length) {
      if (length == 0) {
        length = env.getLengthSec();
      }

      if (length == 0) {
        assert(false); // don't use with empty envelope!!!
        return *this;
      }

      Flt multiplier = length / env.getLengthSec();

      bool endOfEnvelope = false;
      UInt targetPoint = 0;
      Flt targetTime = 0.f;
      Flt fraction = 0.f;
      Flt envelopeValue = env[0].value;

      Flt * ptr = storage.data();

      for (UInt i = 0; i < storage.size(); i++)
      {
        if (!endOfEnvelope && targetTime <= i) { // will always be true on first point

          if (targetPoint + 1 == env.elms()) {
            endOfEnvelope = true;
            targetTime = (float)getLength();
            fraction = 0;
          }
          else {
            Flt startTime = targetTime;
            targetPoint++;
            targetTime = env[targetPoint].time * multiplier * SAMPLERATE;
            Flt period = (targetTime - startTime > 1 ? targetTime - startTime : 1);
            fraction = (env[targetPoint].value - env[targetPoint - 1].value) / period;
          }
        }

        *ptr++ *= envelopeValue;
        envelopeValue += fraction;
      }

      return *this;
    }

    drawableBuffer & YSE::DSP::drawableBuffer::drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue) {
      //Clamp(start, 0, impl->length.load());
      //Clamp(stop, 0, impl->length.load());
      //if (stop < start) return *this; // don't do this

      Flt frac = (stopValue - startValue) / static_cast<Flt>(stop - start > 1 ? stop - start : 1); // don't divide by zero
      Flt * ptr = storage.data();
      Flt value = startValue;
      for (UInt i = start; i < stop; i++) {
        ptr[i] = value;
        value += frac;
      }
      return *this;
    }


    drawableBuffer & YSE::DSP::drawableBuffer::drawLine(UInt start, UInt stop, Flt value) {
      //Clamp(start, 0, impl->length.load());
      //Clamp(stop, 1, impl->length.load());
      //if (stop < start) return *this; // don't do this

      Flt * ptr = storage.data();
      for (UInt i = start; i < stop; i++) {
        ptr[i] = value;
      }
      return *this;
    }

  }
}
