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
}

void YSE::DSP::MODULES::lowPassFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  (*lp).setFrequency(parmFrequency);
  for (UInt i = 0; i < buffer.size(); i++) {
    buffer[i] = (*lp)(buffer[i]);
  }
}