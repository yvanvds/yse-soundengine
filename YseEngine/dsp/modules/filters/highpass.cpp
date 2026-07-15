/*
  ==============================================================================

    highpass.cpp
    Created: 3 Aug 2014 10:09:15pm
    Author:  yvan

  ==============================================================================
*/

#include "highpass.hpp"

YSE::DSP::MODULES::highPassFilter::highPassFilter() : parmFrequency(400.f) {}

YSE::DSP::MODULES::highPassFilter& YSE::DSP::MODULES::highPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::highPassFilter::frequency() {
  return parmFrequency;
}

void YSE::DSP::MODULES::highPassFilter::create() {
  // Per-channel filters are sized lazily in process() once the channel count
  // is known; nothing to allocate up front.
}

void YSE::DSP::MODULES::highPassFilter::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  // Grow/shrink per-channel filter state to match the (possibly changed)
  // channel count. Allocation-free once the count is stable.
  hp.ensure(buffer.size());

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
    }

    hp[ch].setFrequency(parmFrequency);
    result = hp[ch](buffer[ch]);

    calculateImpact(buffer[ch], result);
  }
}