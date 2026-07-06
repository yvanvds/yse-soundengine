/*
  ==============================================================================

    highpassDelay.cpp
    Created: 30 Sep 2015 4:40:29pm
    Author:  yvan

  ==============================================================================
*/

#include "highpassDelay.hpp"

YSE::DSP::MODULES::highPassDelay::highPassDelay() : parmFrequency(1000.f) {}

void YSE::DSP::MODULES::highPassDelay::ensurePreFilter(std::size_t count) {
  hp.ensure(count);
}

void YSE::DSP::MODULES::highPassDelay::applyPreFilter(DSP::buffer& buffer, std::size_t ch) {
  hp[ch].setFrequency(parmFrequency);
  buffer = hp[ch](buffer);
}

YSE::DSP::MODULES::highPassDelay& YSE::DSP::MODULES::highPassDelay::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::highPassDelay::frequency() {
  return parmFrequency;
}