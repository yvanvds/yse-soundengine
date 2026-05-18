/*
  ==============================================================================

    granulator.hpp
    Created: 6 Sep 2015 4:51:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef GRANULATOR_HPP_INCLUDED
#define GRANULATOR_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../dspObject.hpp"
#include "../math.hpp"
#include "../fileBuffer.hpp"
#include <memory>

namespace YSE {
  namespace DSP {
    namespace MODULES {
      /// @cond INTERNAL
      class grain;
      /// @endcond

      /**
       *  @brief Granular synthesis effect as a chainable ``dspObject``.
       *
       *  Maintains a circular pool of recent input audio, then spawns short
       *  "grains" sampled from random positions in that pool. The grain rate,
       *  length, and pitch are configurable, each with a random component for
       *  natural variation. Use for clouds, textures, time-stretch, and
       *  pitch-shifting effects.
       */
      class API granulator : public dspObject {
      public:
        /**
         *  @brief Construct the granulator.
         *
         *  @param poolSize  Size of the circular input buffer in samples.
         *                   Limits how far back into the input the granulator
         *                   can reach. Default is 5 seconds at the engine's
         *                   current sample rate.
         *  @param maxGrains Maximum number of grains alive simultaneously.
         */
        granulator(UInt poolSize = SAMPLERATE * 5, UInt maxGrains = 16);
        virtual ~granulator() {};

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER & buffer);

        /** @brief Set the spawn rate in grains per second. */
        granulator & grainFrequency(UInt value);

        /** @brief Current spawn rate. */
        UInt grainFrequency() { return parmFrequency; }

        /**
         *  @brief Set the grain length.
         *
         *  @param samples Length in samples.
         *  @param random  Random variation around ``samples``, in samples.
         */
        granulator & grainLength(UInt samples, UInt random = 0);

        /** @brief Current grain length. */
        UInt grainLength() { return parmLength; }

        /**
         *  @brief Set the grain pitch shift.
         *
         *  @param pitch  Pitch multiplier (1.0 = unchanged, 2.0 = octave up).
         *  @param random Random pitch variation.
         */
        granulator & grainTranspose(Flt pitch, Flt random = 0);

        /** @brief Current grain pitch multiplier. */
        Flt grainTranspose() { return parmTranspose; }

        /** @brief Set the output gain. */
        granulator & gain(Flt value);

        /** @brief Current output gain. */
        Flt gain() { return parmGain; }

      private:
        UInt poolSize;
        UInt poolPosition;
        std::shared_ptr<DSP::buffer> pool;
        clip limiter;
        
        aUInt parmFrequency;
        aUInt parmLength;
        aUInt parmLengthRandom;
        aFlt parmTranspose;
        aFlt parmTransposeRandom;
        aFlt parmGain;

        Flt leftOverStarts;
        UInt maxGrains;
        std::shared_ptr<std::vector<grain> > grainPool;

        UInt readPos;
      };

    }
  }
}


#endif  // GRANULATOR_HPP_INCLUDED
