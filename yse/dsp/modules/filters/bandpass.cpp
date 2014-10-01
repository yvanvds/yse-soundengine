/*
  ==============================================================================

    bandpass.cpp
    Created: 3 Aug 2014 10:09:23pm
    Author:  yvan

  ==============================================================================
*/

#include "bandpass.hpp"

YSE::DSP::MODULES::bandPassFilter::bandPassFilter() : parmFrequency(400.f), parmQ(1.f) {}

YSE::DSP::MODULES::bandPassFilter & YSE::DSP::MODULES::bandPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::bandPassFilter::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::bandPassFilter & YSE::DSP::MODULES::bandPassFilter::setQ(Flt value) {
  parmQ.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::bandPassFilter::getQ() {
  return parmFrequency;
}

void YSE::DSP::MODULES::bandPassFilter::create() {
  bp.reset(new bandPass);
}

void YSE::DSP::MODULES::bandPassFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();
  (*bp).set(parmFrequency, parmQ);

  for (UInt i = 0; i < buffer.size(); i++) {
    buffer[i] = (*bp)(buffer[i]);
  }
}