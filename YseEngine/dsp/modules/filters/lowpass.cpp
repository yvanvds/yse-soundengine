/*
  ==============================================================================

    lowpass.cpp
    Created: 3 Aug 2014 10:08:59pm
    Author:  yvan

  ==============================================================================
*/

#include "lowpass.hpp"

YSE::DSP::MODULES::lowPassFilter::lowPassFilter() : parmFrequency(1000.f) {}

YSE::DSP::MODULES::lowPassFilter & YSE::DSP::MODULES::lowPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::lowPassFilter::frequency() {
  return parmFrequency;
}

void YSE::DSP::MODULES::lowPassFilter::create() {
  lp.reset(new lowPass);
  result.reset(new buffer);
}

void YSE::DSP::MODULES::lowPassFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
  }

  (*lp).setFrequency(parmFrequency);
  (*result) = (*lp)(buffer[0]);

  calculateImpact(buffer[0], (*result));
}