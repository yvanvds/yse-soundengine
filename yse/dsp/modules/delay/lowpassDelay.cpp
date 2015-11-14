/*
  ==============================================================================

    lowpassDelay.cpp
    Created: 30 Sep 2015 4:40:43pm
    Author:  yvan

  ==============================================================================
*/

#include "lowpassDelay.hpp"

YSE::DSP::MODULES::lowPassDelay::lowPassDelay() : parmFrequency(1000.f) {}

void YSE::DSP::MODULES::lowPassDelay::createPreFilter() {
  lp.reset(new DSP::lowPass);
}

void YSE::DSP::MODULES::lowPassDelay::applyPreFilter(DSP::buffer & buffer) {
  (*lp).setFrequency(parmFrequency);
  buffer = (*lp)(buffer);
}

YSE::DSP::MODULES::lowPassDelay & YSE::DSP::MODULES::lowPassDelay::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::lowPassDelay::frequency() {
  return parmFrequency;
}