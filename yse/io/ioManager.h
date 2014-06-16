/*
  ==============================================================================

    ioManager.h
    Created: 12 Jun 2014 6:29:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IOMANAGER_H_INCLUDED
#define IOMANAGER_H_INCLUDED

#include "ioCallback.h"
#include "..\headers\types.hpp"
#

namespace YSE {
  namespace IO {

   class ioManager {
    public:
      ioManager();
     ~ioManager();

      Bool initialise();
      Bool isRunning ();

      void startDevice  ();
      void stopDevice   ();
      void restartDevice();

      void setCallback(ioCallback * callback);

   private:
     ioCallback * callback;
    };
  }
}



#endif  // IOMANAGER_H_INCLUDED
