/*
  ==============================================================================

    phaser.cpp
    Created: 15 Sep 2015 7:54:15pm
    Author:  yvan

  ==============================================================================
*/

#include "phaser.hpp"
#include <cmath>

YSE::DSP::MODULES::phaser::phaser() {}

void YSE::DSP::MODULES::phaser::create() {
  sawTooth.reset(new saw);
  rzero1.reset(new realOneZeroReversed);
  rzero2.reset(new realOneZeroReversed);
  rzero3.reset(new realOneZeroReversed);
  rzero4.reset(new realOneZeroReversed);
  rpole1.reset(new realOnePole);
  rpole2.reset(new realOnePole);
  rpole3.reset(new realOnePole);
  rpole4.reset(new realOnePole);

  phasor1.reset(new saw);
  phasor2.reset(new saw);
  phasor3.reset(new saw);
  phasor4.reset(new saw);

  clip1.reset(new clip); clip1->set(-0.5, 0.5);
  clip2.reset(new clip); clip2->set(-0.5, 0.5);
  clip3.reset(new clip); clip3->set(-0.5, 0.5);
  clip4.reset(new clip); clip4->set(-0.5, 0.5);

  cos1.reset(new cosine);
  cos2.reset(new cosine);
  cos3.reset(new cosine);
  cos4.reset(new cosine);

  hp.reset(new highPass);
  (*hp).setFrequency(5);
}

void YSE::DSP::MODULES::phaser::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  DSP::buffer & b1 = (*phasor1)(220);
  DSP::buffer & b2 = (*phasor2)(251);
  DSP::buffer & b3 = (*phasor3)(281);
  DSP::buffer & b4 = (*phasor4)(311);

  b1 -= 0.5; b2 -= 0.5; b3 -= 0.5; b4 -= 0.5;
  b1 *= 3; b2 *= 3; b3 *= 3; b4 *= 3;

  b1 = (*clip1)(b1);
  b2 = (*clip2)(b2);
  b3 = (*clip3)(b3);
  b4 = (*clip4)(b4);

  b1 = (*cos1)(b1);
  b2 = (*cos2)(b2);
  b3 = (*cos3)(b3);
  b4 = (*cos4)(b4);

  b1 += b2; b1 += b3; b1 += b4;
  b1 = (*hp)(b1);
  b1 *= 0.2;

  buffer[0] = b1;

  DSP::buffer & s = (*sawTooth)(440);

  Flt * ptr = s.getPtr();
  for (UInt i = 0; i < s.getLength(); i++) {
    *ptr = 1 - 0.03 - 0.6 * std::abs(*ptr - 0.5) * std::abs(*ptr - 0.5);
    ptr++;
  }

  buffer[0] *= 0.2;
  return;
  DSP::buffer & result1 = (*rzero1)(buffer[0], s);
  DSP::buffer & result2 = (*rpole1)(result1, s);
  DSP::buffer & result3 = (*rzero2)(result2, s);
  DSP::buffer & result4 = (*rpole2)(result3, s);
  DSP::buffer & result5 = (*rzero3)(result4, s);
  DSP::buffer & result6 = (*rpole3)(result5, s);
  DSP::buffer & result7 = (*rzero4)(result6, s);
  //buffer[0]            = (*rpole4)(result7, s);
  buffer[0] = result1;
}