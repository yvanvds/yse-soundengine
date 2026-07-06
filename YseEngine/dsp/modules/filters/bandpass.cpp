/*
  ==============================================================================

    bandpass.cpp
    Created: 3 Aug 2014 10:09:23pm
    Author:  yvan

  ==============================================================================
*/

#include "bandpass.hpp"

YSE::DSP::MODULES::bandPassFilter::bandPassFilter() : parmFrequency(400.f), parmQ(1.f) {}

YSE::DSP::MODULES::bandPassFilter& YSE::DSP::MODULES::bandPassFilter::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::bandPassFilter::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::bandPassFilter& YSE::DSP::MODULES::bandPassFilter::setQ(Flt value) {
  parmQ.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::bandPassFilter::getQ() {
  return parmQ;
}

void YSE::DSP::MODULES::bandPassFilter::create() {
  // Per-channel filters are sized lazily in process() once the channel count
  // is known; nothing to allocate up front.
}

void YSE::DSP::MODULES::bandPassFilter::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  // Grow/shrink per-channel filter state to match the (possibly changed)
  // channel count. Allocation-free once the count is stable.
  bp.ensure(buffer.size());

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
    }

    bp[ch].set(parmFrequency, parmQ);
    result = bp[ch](buffer[ch]);

    calculateImpact(buffer[ch], result);
  }
}