/*
  ==============================================================================

    dspSound.h
    Created: 10 Jul 2014 6:08:13pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DSPSOUND_H_INCLUDED
#define DSPSOUND_H_INCLUDED

#include "JuceHeader.h"

namespace YSE {
  namespace SYNTH {

    struct dspSound : public SynthesiserSound {
      dspSound(int channel, int lowLimit, int highLimit, int ID);

      bool appliesToNote(const int noteNumber);
      bool appliesToChannel(const int channel);
      int getID();

    private:
      int channel;
      int lowLimit;
      int highLimit;
      int ID;
    };

  }
}



#endif  // DSPSOUND_H_INCLUDED
