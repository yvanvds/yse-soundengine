#pragma once
#include "../dsp.hpp"
#include "../oscillators.hpp"

/*	when building a new dsp module, take this as an example.
		Also don't forget to:
		1. add a new type to DSP_TYPE in dspElement.h
		2. include your header file to dsp.h
		4. add code to add and remove functions in dsp.cpp
		5. add code to callback function in dsp.cpp
*/

namespace YSE {
	namespace DSP {

    class ringModImpl;
		class API ringModulator : dsp {
    public:
      // these functions are thread safe
			ringModulator();
     ~ringModulator();
      ringModulator& frequency(Flt value);
      Flt            frequency(         );
      ringModulator& level    (Flt value);
      Flt            level    (         );

      // dsp function
			virtual void process(BUFFER buffer);

    private:
      ringModImpl * impl;
		};

	}
}
