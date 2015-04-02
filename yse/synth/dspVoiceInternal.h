/*
  ==============================================================================

    dspVoiceInternal.h
    Created: 10 Jul 2014 8:41:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DSPVOICEINTERNAL_H_INCLUDED
#define DSPVOICEINTERNAL_H_INCLUDED

#include "JuceHeader.h"
#include "dspVoice.hpp"

namespace YSE {
  namespace SYNTH {

    class dspVoiceInternal : public SynthesiserVoice {
    public:
      dspVoiceInternal(dspVoice * dsp, int ID, int voiceID);
      ~dspVoiceInternal();

      bool canPlaySound(SynthesiserSound * sound) override;

      void startNote(int midiNoteNumber, float velocity, SynthesiserSound * sound, int currentPitchWheelPosition) override;
      void stopNote(float velocity, bool allowTailOff) override;
      void pitchWheelMoved(int newValue) override;
      void controllerMoved(int controllerNumber, int newValue) override;
      void renderNextBlock(AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override;
      
      dspVoice * dsp;
      SOUND_STATUS intent;
      int samplesLeft;
      int ID;
      int voiceID;
      bool noteIsCleared;
    };

  }
}



#endif  // DSPVOICEINTERNAL_H_INCLUDED
