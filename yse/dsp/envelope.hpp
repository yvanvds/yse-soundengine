/*
  ==============================================================================

    envelope.hpp
    Created: 18 Jul 2015 6:54:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ENVELOPE_H_INCLUDED
#define ENVELOPE_H_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../sound/audioBuffer.hpp"

namespace YSE {
  namespace DSP {

    /* The envelope class can be used to track the envelope of a sample,
       given a window size. The envelope itself can be applied to to
       other samples and can be saved to a file.
    */
    class API envelope {
    public:

      /* breakpoint definition with a time and a value
      */
      struct breakPoint {
        breakPoint(Flt time, Flt value) : time(time), value(value) {}
        Flt time;
        Flt value;
      };

      // Create an envelope from an audiobuffer, with windowSize in milliseconds
      bool create(YSE::DSP::sample & source, Int windowSize = 15);
      
      // write breakpoint file to disk, returns false on fail
      bool toFile(const char * fileName);
      
    private:
      std::vector<breakPoint> breakPoints;
    };

  }
}



#endif  // ENVELOPE_H_INCLUDED
