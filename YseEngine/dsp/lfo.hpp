/*
  ==============================================================================

    lfo.hpp
    Created: 18 Sep 2015 6:10:24pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LFO_HPP_INCLUDED
#define LFO_HPP_INCLUDED

#include "drawableBuffer.hpp"

namespace YSE {
  namespace DSP {

    /** @brief Available LFO shapes for ``lfo`` and for ``dspObject::lfoType``. */
    enum LFO_TYPE {
      LFO_NONE,           ///< No modulation.
      LFO_SAW,            ///< Sawtooth, rising 0 → 1.
      LFO_SAW_REVERSED,   ///< Reverse sawtooth, falling 1 → 0.
      LFO_TRIANGLE,       ///< Symmetric triangle.
      LFO_SINE,           ///< Sine, mapped to [0, 1].
      LFO_SQUARE,         ///< Square wave alternating between 0 and 1.
      LFO_RANDOM,         ///< Stepped random (sample-and-hold) between 0 and 1.
    };

    /**
     *  @brief Low-frequency modulator.
     *
     *  Generates a control-rate signal in [0, 1] that can be used to modulate
     *  filter cutoff, oscillator pitch, panning, or any other DSP parameter
     *  that expects a buffer.
     */
    class API lfo {
    public:
      lfo();

      /**
       *  @brief Produce the next LFO block.
       *
       *  @param type      Waveform shape (see ``LFO_TYPE``).
       *  @param frequency Modulation rate in Hz.
       *  @param size      Block size in samples.
       *  @return Buffer of values in [0.0, 1.0].
       */
      buffer & operator()(LFO_TYPE type, Flt frequency, UInt size = STANDARD_BUFFERSIZE);

    private:
      drawableBuffer result;
      Flt cursor;
      LFO_TYPE previousType;

      UInt lineLength;
      Flt currentLineValue, previousLineValue;

    };

  }
}



#endif  // LFO_HPP_INCLUDED
