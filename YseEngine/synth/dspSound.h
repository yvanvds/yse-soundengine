/*
  ==============================================================================

    dspSound.h
    Created: 10 Jul 2014 6:08:13pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DSPSOUND_H_INCLUDED
#define DSPSOUND_H_INCLUDED

namespace YSE {
  namespace SYNTH {

    struct dspSound {
      dspSound(int channel, int lowLimit, int highLimit, int ID);

      virtual bool appliesToNote(int noteNumber);
      virtual bool appliesToChannel(int channel);
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
