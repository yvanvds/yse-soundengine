/*
  ==============================================================================

    time.h
    Created: 30 Jan 2014 5:31:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

#include <ctime>
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    class time {
    public:

      void update();

      Flt delta();

      time() { current = last = 0; d = 0.0f; }
    private:
      ULong current;
      ULong last;
      Flt d; // delta
    };

    time & Time(); // updates every time update is called
    time & DeviceTime(); // updates every time the devices asks for a buffer
  }
}



#endif  // TIME_H_INCLUDED
