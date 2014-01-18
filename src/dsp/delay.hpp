#pragma once
#include "sample.hpp"

namespace YSE {
	extern UInt sampleRate;
	namespace DSP {

    class delayImpl;
		class API delay {
    public:
			delay& set(UInt size); // change line length

      // TODO: how do these functions compare to other dsp functions?
      // following functions should only be called from the audio callback
      delay& update(SAMPLE s); // add input signal to delay
			
      // using a return SAMPLE seems more logical.
      // However, it is perfectly normal to read parts of a delay
      // line into different samples after updating it. Using an
      // internal buffer and overwriting that would invalidate every
      // previous buffer reference.
			delay& get(sample& s, UInt delayTime); // read from delay at fixed point
      delay& get(sample& s, SAMPLE delayTime); // read from delay at variable point

			delay();
		 ~delay(); 

		private:
      delayImpl * impl;
		};

		void readInterpolated(SAMPLE ctrl, sample& out, SAMPLE buffer, UInt &pos);

	}
}