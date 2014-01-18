#pragma once
#include "sample.hpp"

namespace YSE {
	namespace DSP {
    // basic converters
    Flt API MidiToFreq(Flt note); 
    Flt API FreqToMidi(Flt freq);

    // clip audio signal at low and high value
    class clipImpl;
		class API clip {
    public:
      // use from anywhere
			clip& set			(Flt low, Flt high); 
			clip& setLow	(Flt low					); 
			clip& setHigh	(					Flt high); 
			clip();
     ~clip();

      // use in DSP
			SAMPLE operator()(SAMPLE in);

		private:
      clipImpl * impl;
		};

		// reciprocal square root good to 8 mantissa bits
    // use in DSP only
		class API rSqrt {
    public:
			SAMPLE operator()(SAMPLE in);
			rSqrt();
		private:
			sample buffer;
		};

		// square root good to 8 mantissa bits
    // use in DSP only
		class API sqrt {
    public:
			SAMPLE operator()(SAMPLE in);
			sqrt();
		private:
			sample buffer;
		};

		// calculates difference between signal and first exceeding integer
    // use in DSP only
		class API wrap {
    public:
      SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

    // use in DSP only
		class API midiToFreq {
    public:	
      SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

    // use in DSP only
		class API freqToMidi {
    public:
      SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

    // use in DSP only
		class API dbToRms {
    public:
      SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

		// use in DSP only
		class API rmsToDb {
    public:
			SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

		// use in DSP only
		class API dbToPow {
    public:
			SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

		// use in DSP only
		class API powToDb {
    public:
			SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

		// use in DSP only
		class API pow {
    public:
			SAMPLE operator()(SAMPLE in1, SAMPLE in2);
		private:
			sample buffer;
		};

		// use in DSP only
		class API exp {
    public:
			SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};

		// use in DSP only
		class API log {
    public:
			SAMPLE operator()(SAMPLE in1, SAMPLE in2);
		private:
			sample buffer;
		};

		// use in DSP only
		class API abs {
    public:
			SAMPLE operator()(SAMPLE in);
		private:
			sample buffer;
		};
	}
}