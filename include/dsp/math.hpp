/*
  ==============================================================================

    math.h
    Created: 31 Jan 2014 2:54:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MATH_H_INCLUDED
#define MATH_H_INCLUDED

#include "../headers/types.hpp"
#include "sample.hpp"

namespace YSE {
  namespace DSP {
    // basic converters
    Flt API MidiToFreq(Flt note);
    Flt API FreqToMidi(Flt freq);

    // clip audio signal at low and high value
    class API clip {
    public:
      // use from anywhere
      clip& set(Flt low, Flt high);
      clip& setLow(Flt low);
      clip& setHigh(Flt high);
      clip() : low(-1.0f), high(1.0f) {}

      // use in DSP
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);

    private:
      aFlt low;
      aFlt high;
      sample buffer;
    };

    // reciprocal square root good to 8 mantissa bits
    // use in DSP only
    class API rSqrt {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
      rSqrt();
    private:
      sample buffer;
    };

    // square root good to 8 mantissa bits
    // use in DSP only
    class API sqrt {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
      sqrt();
    private:
      sample buffer;
    };

    // calculates difference between signal and first exceeding integer
    // use in DSP only
    class API wrap {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API midiToFreq {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API freqToMidi {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API dbToRms {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API rmsToDb {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API dbToPow {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API powToDb {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API pow {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in1, AUDIOBUFFER & in2);
    private:
      sample buffer;
    };

    // use in DSP only
    class API exp {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };

    // use in DSP only
    class API log {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in1, AUDIOBUFFER & in2);
    private:
      sample buffer;
    };

    // use in DSP only
    class API abs {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
    private:
      sample buffer;
    };
  }
}

#endif  // MATH_H_INCLUDED
