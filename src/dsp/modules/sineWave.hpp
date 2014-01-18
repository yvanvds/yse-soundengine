#pragma once
#include "../dsp.hpp"
#include "../oscillators.hpp"

namespace YSE {
	namespace DSP {
    class sineWaveImpl;
		class API sineWave : public dspSource {
    public:
      // object is thread safe
      sineWave();
     ~sineWave();

      // change the frequency instantly, or wait for the next note on status
      void      frequency(Flt value);
      Flt       frequency(         );

			virtual void process(SOUND_STATUS & intent, Int & latency); // use only during DSP

    private:
      sineWaveImpl * impl;
		};

	}
}
