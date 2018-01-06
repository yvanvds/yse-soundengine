/*
  ==============================================================================

    synthMessage.h
    Created: 9 Jul 2014 12:36:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYNTHMESSAGE_H_INCLUDED
#define SYNTHMESSAGE_H_INCLUDED

#include "synth.hpp"
#include "../headers/types.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace SYNTH {

    class messageObject {
    public:
      MESSAGE ID;

      union content {
        Bool boolValue;
        Flt floatValue;
        void * ptr;
        U16 uIntValue[3];
        Flt vecValue[3];
        Int intValue[3];
        struct {
          Int channel;
          Bool value;
        } cb;
      };
    };
  }
}



#endif  // SYNTHMESSAGE_H_INCLUDED
