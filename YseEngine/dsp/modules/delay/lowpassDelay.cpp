/*
  ==============================================================================

    lowpassDelay.cpp
    Created: 30 Sep 2015 4:40:43pm
    Author:  yvan

  ==============================================================================
*/

#include "lowpassDelay.hpp"

YSE::DSP::MODULES::lowPassDelay::lowPassDelay() : parmFrequency(1000.f) {}

void YSE::DSP::MODULES::lowPassDelay::ensurePreFilter(std::size_t count) {
  lp.ensure(count);
}

void YSE::DSP::MODULES::lowPassDelay::applyPreFilter(DSP::buffer& buffer, std::size_t ch) {
  lp[ch].setFrequency(parmFrequency);
  buffer = lp[ch](buffer);
}

YSE::DSP::MODULES::lowPassDelay& YSE::DSP::MODULES::lowPassDelay::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::lowPassDelay::frequency() {
  return parmFrequency;
}