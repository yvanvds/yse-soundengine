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
      class grain;

      class API granulator : public dspObject {
      public:
        granulator(UInt poolSize = 44100 * 5, UInt maxGrains = 16);
        virtual ~granulator() {};

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

        /* Grains per second */
        granulator & grainFrequency(UInt value);
        UInt grainFrequency() { return parmFrequency; }
        
        /* Length of each grain */
        granulator & grainLength(UInt samples, UInt random = 0);
        UInt grainLength() { return parmLength; }
        
        /* Grain transposition */
        granulator & grainTranspose(Flt pitch, Flt random = 0);
        Flt grainTranspose() { return parmTranspose; }

        granulator & gain(Flt value);
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
