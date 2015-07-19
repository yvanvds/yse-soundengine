/*
  ==============================================================================

    audioBuffer.hpp
    Created: 30 Mar 2015 7:22:34pm
    Author:  yvan

  ==============================================================================
*/

#ifndef AUDIOBUFFER_HPP_INCLUDED
#define AUDIOBUFFER_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../dsp/sample.hpp"

namespace YSE {

  /**
    The AudioBuffer class is intended for analysis or manipulation of complete
    sound files. It is different from a DSP audio buffer in that it can be used to
    load audio files from disk and can be passed to a sound object or virtual synth.

    On the other hand, if you just intend to play a soundfile you should use the
    sound object instead because it will perform better.
  */

  namespace INTERNAL {
    class soundFile;
  }

  class API audioBuffer {
  public:
    audioBuffer();

    Bool create(const char * fileName);
    Bool isValid() const;

    Int getNumChannels() const;
    MULTICHANNELBUFFER & getChannels();
    AUDIOBUFFER & getChannel(Int nr);

    Bool saveToFile(const char * fileName);

  private:
    MULTICHANNELBUFFER buffer;
    Flt sampleRateAdjustment;
    Bool valid;

    friend class YSE::INTERNAL::soundFile;
  };

}



#endif  // AUDIOBUFFER_HPP_INCLUDED
