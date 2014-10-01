/*
  ==============================================================================

    highpass.cpp
    Created: 3 Aug 2014 10:09:15pm
    Author:  yvan

  ==============================================================================
*/

#include "highpass.hpp"


YSE::DSP::MODULES::highPassFilter::highPassFilter() : parmFrequency(400.f) {}

YSE::DSP::MODULES::highPassFilter & YSE::DSP::MODULES::highPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::highPassFilter::frequency() {
  return parmFrequency;
}

void YSE::DSP::MODULES::highPassFilter::create() {
  hp.reset(new highPass);
}

void YSE::DSP::MODULES::highPassFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();
  (*hp).setFrequency(parmFrequency);

  for (UInt i = 0; i < buffer.size(); i++) {
    buffer[i] = (*hp)(buffer[i]);
  }
}