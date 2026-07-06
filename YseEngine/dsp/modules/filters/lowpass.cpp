/*
  ==============================================================================

    lowpass.cpp
    Created: 3 Aug 2014 10:08:59pm
    Author:  yvan

  ==============================================================================
*/

#include "lowpass.hpp"

YSE::DSP::MODULES::lowPassFilter::lowPassFilter() : parmFrequency(1000.f) {}

YSE::DSP::MODULES::lowPassFilter& YSE::DSP::MODULES::lowPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::lowPassFilter::frequency() {
  return parmFrequency;
}

void YSE::DSP::MODULES::lowPassFilter::create() {
  // Per-channel filters are sized lazily in process() once the channel count
  // is known; nothing to allocate up front.
}

void YSE::DSP::MODULES::lowPassFilter::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  // Grow/shrink per-channel filter state to match the (possibly changed)
  // channel count. Allocation-free once the count is stable.
  lp.ensure(buffer.size());

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
    }

    lp[ch].setFrequency(parmFrequency);
    result = lp[ch](buffer[ch]);

    calculateImpact(buffer[ch], result);
  }
}