/*
  ==============================================================================

    dspVoiceInternal.h
    Created: 10 Jul 2014 8:41:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DSPVOICEINTERNAL_H_INCLUDED
#define DSPVOICEINTERNAL_H_INCLUDED

#include "dspVoice.hpp"
#include "dspSound.h"

//namespace YSE {
//  namespace SYNTH {
//
//    class dspVoiceInternal {
//    public:
//      dspVoiceInternal(dspVoice * dsp, int ID, int voiceID);
//      ~dspVoiceInternal();
//
//      bool canPlaySound(dspSound * sound);
//
//      void startNote(int midiNoteNumber, float velocity, dspSound * sound, int currentPitchWheelPosition);
//      void stopNote(float velocity, bool allowTailOff);
//      void pitchWheelMoved(int newValue);
//      void controllerMoved(int controllerNumber, int newValue);
//      void renderNextBlock(MULTICHANNELBUFFER & outputBuffer, int startSample, int numSamples);
//      
//    private:
//      void clearCurrentNote();
//
//      dspVoice * dsp;
//      SOUND_STATUS intent;
//      int samplesLeft;
//      int ID;
//      int voiceID;
//      bool noteIsCleared;
//
//      
//    };
//
//  }
//}



#endif  // DSPVOICEINTERNAL_H_INCLUDED
