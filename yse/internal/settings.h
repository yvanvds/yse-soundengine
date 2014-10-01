/*
  ==============================================================================

    settings.h
    Created: 30 Jan 2014 5:31:34pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SETTINGS_H_INCLUDED
#define SETTINGS_H_INCLUDED

#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    class settings {
    public:
      Flt dopplerScale;
      Flt distanceFactor;
      Flt rolloffScale;

      settings() : dopplerScale(1.f), distanceFactor(1.f), rolloffScale(1.f) {}
    };

    settings & Settings();
  }
}



#endif  // SETTINGS_H_INCLUDED
