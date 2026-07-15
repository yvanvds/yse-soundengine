/*
  ==============================================================================

    ringModulator.cpp
    Created: 31 Jan 2014 2:56:31pm
    Author:  yvan

  ==============================================================================
*/

#include "ringModulator.hpp"

YSE::DSP::ringModulator::ringModulator() : parmFrequency(440.f), parmLevel(0.5f) {}

YSE::DSP::ringModulator& YSE::DSP::ringModulator::frequency(Flt value) {
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::ringModulator::frequency() {
  return parmFrequency;
}

void YSE::DSP::ringModulator::create() {
  sineGen.reset(new sine);
  result.reset(new buffer);
}

void YSE::DSP::ringModulator::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // A single carrier modulates every channel, so generate the sine once per
  // block (advancing its phase once, exactly as in the mono case) and apply it
  // to each channel in turn.
  YSE::DSP::buffer& sin = (*sineGen)(parmFrequency, buffer[0].getLength());

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result->getLength()) {
      result->resize(buffer[ch].getLength());
    }

    (*result) = buffer[ch];
    (*result) *= sin;

    calculateImpact(buffer[ch], (*result));
  }
}