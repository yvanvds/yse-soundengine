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
#include "buffer.hpp"

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
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

    private:
      aFlt low;
      aFlt high;
      YSE::DSP::buffer buffer;
    };

    // reciprocal square root good to 8 mantissa bits
    // use in DSP only
    class API rSqrt {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      rSqrt();
    private:
      YSE::DSP::buffer buffer;
    };

    // square root good to 8 mantissa bits
    // use in DSP only
    class API sqrt {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      sqrt();
    private:
      YSE::DSP::buffer buffer;
    };

    // calculates difference between signal and first exceeding integer
    // use in DSP only
    class API wrap {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API midiToFreq {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API freqToMidi {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API dbToRms {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API rmsToDb {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API dbToPow {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API powToDb {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API pow {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in1, YSE::DSP::buffer & in2);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API exp {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API log {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in1, YSE::DSP::buffer & in2);
    private:
      YSE::DSP::buffer buffer;
    };

    // use in DSP only
    class API abs {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    class API inverter {
    public:
      // if zeroToOne is true, values are inverted as 1 - value
      // else values are inverted as value = -value
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in, bool zeroToOne = false);
    private:
      YSE::DSP::buffer buffer;
    };
  }
}

#endif  // MATH_H_INCLUDED
