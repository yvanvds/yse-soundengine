/*
  ==============================================================================

    ringModulator.cpp
    Created: 31 Jan 2014 2:56:31pm
    Author:  yvan

  ==============================================================================
*/

#include "ringModulator.hpp"

YSE::DSP::ringModulator::ringModulator() : parmFrequency(440.f), parmLevel(0.5f) {}

YSE::DSP::ringModulator& YSE::DSP::ringModulator::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::ringModulator::frequency() {
  return parmFrequency;
}

YSE::DSP::ringModulator& YSE::DSP::ringModulator::level(Flt value) {
  parmLevel.store(value);
  return *this;
}

Flt YSE::DSP::ringModulator::level() {
  return parmLevel;
}

void YSE::DSP::ringModulator::create() {
  sineGen.reset(new sine);
  extra.reset(new sample);
}

void YSE::DSP::ringModulator::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  // generate sine wave at wanted frequency
  AUDIOBUFFER & sin = (*sineGen)(parmFrequency, buffer[0].getLength());

  Flt level = parmLevel;
  for (UInt i = 0; i < buffer.size(); i++) {
    // make a copy of the original, this will add the dry sound to the output
    (*extra) = buffer[i];
    // adjust volume of dry sound
    (*extra) *= static_cast<Flt>(1 - level);
    // combine input with sine to get a ring modulator effect
    buffer[i] *= sin;
    // adjust volume of wet sound
    buffer[i] *= level;

    // combine wet and dry sound 
    buffer[i] += (*extra);
  }
}