/*
  ==============================================================================

    reverb.h
    Created: 1 Feb 2014 7:02:58pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERB_H_INCLUDED
#define REVERB_H_INCLUDED

#include "utils/vector.hpp"
#include "headers/enums.hpp"

namespace YSE {

  class API reverb {
  public:
    reverb& pos(const  Vec &value); Vec pos();
    reverb& size(Flt  value); Flt size();
    reverb& rolloff(Flt  value); Flt rolloff();
    reverb& active(Bool value); Bool active();
    reverb& roomsize(Flt  value); Flt roomsize();
    reverb& damp(Flt  value); Flt damp();
    reverb& wet(Flt  value); Flt wet();
    reverb& dry(Flt  value); Flt dry();
    reverb& modFreq(Flt  value); Flt modFreq();
    reverb& modWidth(Flt  value); Flt modWidth();

    reverb& reflectionTime(Int reflection, Int value); Int reflectionTime(Int reflection); // reflection must be from 0 to 3
    reverb& reflectionGain(Int reflection, Flt value); Flt reflectionGain(Int reflection);

    reverb& preset(REVERB_PRESET value);

    reverb& release();

    reverb();
   ~reverb();

  private:
    Bool _active;
    Bool connectedToManager; 
    Flt _roomsize, _damp, _wet, _dry;

    Flt _modFrequency, _modWidth; // modulation

    Int _earlyPtr[4]; // early reflections
    Flt _earlyGain[4];

    Vec _position;
    Flt _size, _rolloff;

    REVERB_PRESET _preset;

    Bool global;
    friend class INTERNAL::reverbManager;
  };

  reverb & GlobalReverb();
}



#endif  // REVERB_H_INCLUDED
