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
  result.reset(new buffer);
}

void YSE::DSP::MODULES::highPassFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
  }

  (*hp).setFrequency(parmFrequency);
  (*result) = (*hp)(buffer[0]);
  
  calculateImpact(buffer[0], (*result));
}