/*
  ==============================================================================

    time.h
    Created: 30 Jan 2014 5:31:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

#include "../headers/types.hpp"
#include "JuceHeader.h"
#include <ctime>

namespace YSE {
  namespace INTERNAL {

    class time {
    public:

      void update();

      Flt delta();

      time() { current = last = 0; d = 0.0f; }
      ~time() { clearSingletonInstance(); }
      juce_DeclareSingleton(time, true);
    private:
      ULong current;
      ULong last;
      Flt d; // delta
    };

  }
}



#endif  // TIME_H_INCLUDED
