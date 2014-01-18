#pragma once
#include "sample.hpp"

namespace YSE {
	extern UInt sampleRate;
	
	namespace DSP {
    class rampImpl; 
		class API ramp : public sample {
    public:
			ramp& set     (Flt target, Int time = 0);
      ramp& setIfNew(Flt target, Int time = 0);
			ramp& stop    (                        );
			ramp& update  (                        );
      // TODO: check update and operator functions for consistency with other DSP objects
			SAMPLE operator()();
			SAMPLE getSample ();
			Flt    getValue  ();

			ramp();
     ~ramp();

		private:
      rampImpl * impl;
		};


    class lintImpl;
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
     ~lint();
    private:
      lintImpl * impl;
    };



		// these functions are limited to the length of the buffer
		void FastFadeIn(sample& s, UInt length);
		void FastFadeOut(sample& s, UInt length);
		void ChangeGain(sample& s, Flt currentGain, Flt newGain, UInt length);


	}
}