/*
  ==============================================================================

    dspVoiceInternal.cpp
    Created: 10 Jul 2014 8:41:01pm
    Author:  yvan

  ==============================================================================
*/

#include "dspVoiceInternal.h"
#include "dspSound.h"

YSE::SYNTH::dspVoiceInternal::dspVoiceInternal(dspVoice * dsp, int ID, int voiceID) 
: dsp(dsp->clone()), intent(SS_STOPPED), samplesLeft(0), ID(ID), voiceID(voiceID) {

}

YSE::SYNTH::dspVoiceInternal::~dspVoiceInternal() {
  if (dsp != nullptr) delete dsp;
  dsp = nullptr;
}

bool YSE::SYNTH::dspVoiceInternal::canPlaySound(SynthesiserSound * sound) {
  if (intent != SS_STOPPED) return false;
  
  return (((dspSound*)(sound))->getID() == ID);
}

void YSE::SYNTH::dspVoiceInternal::startNote(int midiNoteNumber, float velocity, SynthesiserSound * sound, int currentPitchWheelPosition) {
  dsp->frequency(static_cast<Flt>(midiNoteNumber));
  dsp->velocity(velocity);
  // if we start a new note, never use leftover samples
  samplesLeft = 0;
  noteIsCleared = false;
  intent = SS_WANTSTOPLAY;
}

void YSE::SYNTH::dspVoiceInternal::stopNote(float velocity, bool allowTailOff) {
  if (intent != SS_STOPPED) {
    intent = SS_WANTSTOSTOP;
  }
  //} 
  //else {
  //  intent = SS_STOPPED;
  //  clearCurrentNote();
  //  noteIsCleared = true;
  //}
}

void YSE::SYNTH::dspVoiceInternal::pitchWheelMoved(int newValue) {

}

void YSE::SYNTH::dspVoiceInternal::controllerMoved(int controllerNumber, int newValue) {

}

void YSE::SYNTH::dspVoiceInternal::renderNextBlock(AudioSampleBuffer& outputBuffer, int startSample, int numSamples) {
  if (intent == SS_STOPPED && samplesLeft == 0) {
    return;
  }

  while (numSamples > 0) {
    if (samplesLeft == 0) {
      dsp->process(intent);
      samplesLeft = dsp->samples[0].getLength();
      dsp->samples[0].cursor = dsp->samples[0].getPtr();
    }
    int samplesToCopy = Min(samplesLeft, numSamples);
    for (int i = outputBuffer.getNumChannels(); --i >= 0;) {
      outputBuffer.addFrom(i, startSample, dsp->samples[0].cursor, samplesToCopy);
    }
    dsp->samples[0].cursor += samplesToCopy;
    startSample += samplesToCopy;
    samplesLeft -= samplesToCopy;
    numSamples -= samplesToCopy;

    if (intent == SS_STOPPED && samplesLeft == 0) {
      clearCurrentNote();
      return;
    }
  }
}