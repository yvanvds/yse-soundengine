/*
  ==============================================================================

    ramp.h
    Created: 31 Jan 2014 2:55:23pm
    Author:  yvan

  ==============================================================================
*/

#ifndef RAMP_H_INCLUDED
#define RAMP_H_INCLUDED

#include "../headers/defines.hpp"
#include "buffer.hpp"

namespace YSE {

  namespace DSP {
    class API ramp : public buffer {
    public:
      ramp& set(Flt target, Int time = 0);
      ramp& setIfNew(Flt target, Int time = 0);
      ramp& stop();
      ramp& update();
      // TODO: check update and operator functions for consistency with other DSP objects
      YSE::DSP::buffer & operator()();
      YSE::DSP::buffer & getSample();
      Flt    getValue();

      ramp();
      ramp(ramp &);

    private:
      aFlt target;
      aFlt time;
      aFlt current;
      aInt ticksLeft;
      aBool reTarget;

      Flt _1overN;
      Flt _dspTickToMSEC;
      Flt inc, bigInc;
      Int nTicks;
      Int l;
      Flt * ptr;
      Flt f;
    };

    class API lint {
      // Linear interpolation towards target over time.
      // This class does not use an audio buffer but adjusts
      // one step for every time update is called.
      // Update expects to be called at every new buffer
    public:
      lint& set(Flt target, Int time); // set new target and time
      lint& setIfNew(Flt target, Int time); // set only if target is different from current target
      lint& stop(); // sets target to current value
      lint& update(); // call this once for every buffer update
      Flt target(); // returns target
      Flt operator()(); // returns current value
      lint();
    private:
      aFlt targetValue, currentValue, step;
      aBool up, calculate;
      Flt stepSecond;
    };


    // these functions are limited to the length of the buffer
    void FastFadeIn(YSE::DSP::buffer & s, UInt length);
    void FastFadeOut(YSE::DSP::buffer & s, UInt length);
    void ChangeGain(YSE::DSP::buffer & s, Flt currentGain, Flt newGain, UInt length);


  }
}



#endif  // RAMP_H_INCLUDED
