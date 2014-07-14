/*
  ==============================================================================

    dspSound.cpp
    Created: 10 Jul 2014 6:08:13pm
    Author:  yvan

  ==============================================================================
*/

#include "dspSound.h"

YSE::SYNTH::dspSound::dspSound(int channel, int lowLimit, int highLimit, int ID) 
  : channel(channel)
  , lowLimit(lowLimit)
  , highLimit(highLimit)
  , ID(ID)
{
}

bool YSE::SYNTH::dspSound::appliesToNote(const int noteNumber) {
  if (noteNumber < lowLimit) return false;
  if (noteNumber > highLimit) return false;
  return true;
}

bool YSE::SYNTH::dspSound::appliesToChannel(const int channel) {
  if (this->channel == 0) return true;
  return (this->channel == channel);
}

int YSE::SYNTH::dspSound::getID() {
  return ID;
}
