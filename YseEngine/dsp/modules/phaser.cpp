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
  rzero1.reset(new realOneZeroReversed);
  rzero2.reset(new realOneZeroReversed);
  rzero3.reset(new realOneZeroReversed);
  rzero4.reset(new realOneZeroReversed);
  rpole1.reset(new realOnePole);
  rpole2.reset(new realOnePole);
  rpole3.reset(new realOnePole);
  rpole4.reset(new realOnePole);
  result.reset(new buffer);
}

YSE::DSP::MODULES::phaser & YSE::DSP::MODULES::phaser::frequency(Flt value) {
  if (value < 0) value = 0;
  parmFrequency.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::phaser::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::phaser & YSE::DSP::MODULES::phaser::range(Flt value) {
  YSE::Clamp(value, 0, 0.5);
  parmRange.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::phaser::range() {
  return parmRange;
}

void YSE::DSP::MODULES::phaser::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
  }

  (*result) = buffer[0];

  DSP::buffer & s = (*triangle)(YSE::DSP::LFO_TRIANGLE, parmFrequency);

  s *= parmRange;
  s += 0.98f - parmRange;

  //buffer[0] *= 0.2;
  //return;
  DSP::buffer & result1 = (*rzero1)((*result), s);
  DSP::buffer & result2 = (*rpole1)(result1, s);
  DSP::buffer & result3 = (*rzero2)(result2, s);
  DSP::buffer & result4 = (*rpole2)(result3, s);
  DSP::buffer & result5 = (*rzero3)(result4, s);
  DSP::buffer & result6 = (*rpole3)(result5, s);
  DSP::buffer & result7 = (*rzero4)(result6, s);
  (*result) += (*rpole4)(result7, s);
  
  calculateImpact(buffer[0], (*result));
}