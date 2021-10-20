/*
  ==============================================================================

    ADSRenvelope.hpp
    Created: 26 Jul 2015 3:25:02pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ADSRENVELOPE_HPP_INCLUDED
#define ADSRENVELOPE_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    class API ADSRenvelope {
    public:
      struct breakPoint {
        breakPoint(Flt time, Flt value, Flt coef, Bool loopStart = false, Bool loopEnd = false) 
          : time(time), 
            value(value), 
            coef(coef), 
            loopStart(loopStart), 
            loopEnd(loopEnd) {}
        Flt time, value, coef;
        Bool loopStart, loopEnd;
      };

      enum STATE {
        ATTACK,
        RESUME,
        RELEASE,
      };

      // strange things will happen if breakpoints are not inserted in
      // order of time!
      void addPoint(const breakPoint & point);
      
      // create envelope from breakPoints, be sure to do this
      // before using the object in DSP functions
      void generate();

      YSE::DSP::buffer & operator()(STATE state, UInt length = STANDARD_BUFFERSIZE);

      void saveToFile(const char * fileName);

      inline Bool isAtEnd() { return endReached; }

    private:
      std::vector<breakPoint> breakPoints;
      buffer envelope, result;
      Flt * phase;
      Flt * loopStart;
      Flt * loopEnd;
      Flt * envelopeEnd;
      Bool looping;
      Bool endReached;
    };

  }
}



#endif  // ADSRENVELOPE_HPP_INCLUDED
