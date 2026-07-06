/*
  ==============================================================================

    phaser.cpp
    Created: 15 Sep 2015 7:54:15pm
    Author:  yvan

  ==============================================================================
*/

#include "phaser.hpp"
#include "../../utils/misc.hpp"
#include <cmath>

YSE::DSP::MODULES::phaser::phaser() : parmFrequency(0.3), parmRange(0.1) {}

void YSE::DSP::MODULES::phaser::create() {
  triangle.reset(new lfo);
  // Per-channel all-pass cascades are sized lazily in process() once the
  // channel count is known; nothing to allocate up front.
}

YSE::DSP::MODULES::phaser& YSE::DSP::MODULES::phaser::frequency(Flt value) {
  if (value < 0) value = 0;
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::phaser::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::phaser& YSE::DSP::MODULES::phaser::range(Flt value) {
  YSE::Clamp(value, 0, 0.5);
  parmRange.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::phaser::range() {
  return parmRange;
}

void YSE::DSP::MODULES::phaser::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Grow/shrink per-channel cascades to the channel count. Allocation-free
  // once the count is stable.
  chain.ensure(buffer.size());

  // The triangle LFO drives the whole buffer with a single sweep, so compute
  // its coefficient signal once per block (advancing the LFO once, exactly as
  // in the mono case).
  DSP::buffer& s = (*triangle)(YSE::DSP::LFO_TRIANGLE, parmFrequency);
  s *= parmRange;
  s += 0.98f - parmRange;

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
    }

    result = buffer[ch];

    allpassChain& c = chain[ch];
    DSP::buffer& result1 = c.rzero1(result, s);
    DSP::buffer& result2 = c.rpole1(result1, s);
    DSP::buffer& result3 = c.rzero2(result2, s);
    DSP::buffer& result4 = c.rpole2(result3, s);
    DSP::buffer& result5 = c.rzero3(result4, s);
    DSP::buffer& result6 = c.rpole3(result5, s);
    DSP::buffer& result7 = c.rzero4(result6, s);
    result += c.rpole4(result7, s);

    calculateImpact(buffer[ch], result);
  }
}