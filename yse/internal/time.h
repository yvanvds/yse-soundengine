/*
  ==============================================================================

    time.h
    Created: 30 Jan 2014 5:31:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

// TODO: use chrono instead of ctime
#include <ctime>
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    class time {
    public:

      void update();

      Flt delta();
      
      // time since application start (in seconds)
      Int appTime();
      
      // time since application start (in milliseconds)
      Int appTimeMs();

      time() { current = last = 0; d = 0.0f; }
    private:
      ULong current;
      ULong last;
      Flt d; // delta
    };

    time & Time();
  }
}



#endif  // TIME_H_INCLUDED
