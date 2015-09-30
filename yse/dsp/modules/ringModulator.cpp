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

void YSE::DSP::ringModulator::create() {
  sineGen.reset(new sine);
  result.reset(new buffer);
}

void YSE::DSP::ringModulator::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
  }

  // generate sine wave at wanted frequency
  YSE::DSP::buffer & sin = (*sineGen)(parmFrequency, buffer[0].getLength());

  (*result) = buffer[0];
  (*result) *= sin;
  
  calculateImpact(buffer[0], (*result));
}