/*
  ==============================================================================

    sweep.cpp
    Created: 4 Sep 2015 10:49:34am
    Author:  yvan

  ==============================================================================
*/

#include "sweep.hpp"
#include "../../../utils/misc.hpp"
#include "../../math.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {
      buffer mtofTable(0);
    }
  }
}

YSE::DSP::MODULES::sweepFilter::sweepFilter(SHAPE shape) : parmSpeed(0), parmDepth(0), parmFrequency(0), shape(shape) {

}

YSE::DSP::MODULES::sweepFilter & YSE::DSP::MODULES::sweepFilter::frequency(Int value) {
  Clamp(value, 0, 100);
  parmFrequency.store(value);
  return *this;
}

Int YSE::DSP::MODULES::sweepFilter::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::sweepFilter & YSE::DSP::MODULES::sweepFilter::depth(Int value) {
  Clamp(value, 0, 100);
  parmDepth.store(value);
  return *this;
}

Int YSE::DSP::MODULES::sweepFilter::depth() {
  return parmDepth;
}

YSE::DSP::MODULES::sweepFilter & YSE::DSP::MODULES::sweepFilter::speed(Flt value) {
  parmSpeed.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::sweepFilter::speed() {
  return parmSpeed;
}

void YSE::DSP::MODULES::sweepFilter::create() {
  table.reset(new wavetable);
  
  switch (shape) {
    case TRIANGLE: table->createTriangle(8, SAMPLERATE); break;
    case SAW: table->createSaw(8, SAMPLERATE); break;
    case SQUARE: table->createSquare(8, SAMPLERATE); break;
  }

  osc.reset(new oscillator);
  osc->initialize(*table);
  filter.reset(new vcf);
  filter->sharpness(2);
  result.reset(new DSP::buffer);
  interpolator.reset(new DSP::interpolate4);

  if (!mtofTable.getLength()) {
    mtofTable.resize(130);
    Flt * t = mtofTable.getPtr();
    for (int i = 0; i < 130; i++) {
      *t++ = MidiToFreq((float)i);
    }
  }

  interpolator->source(mtofTable);
}

void YSE::DSP::MODULES::sweepFilter::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
  }

  (*result) = (*osc)(parmSpeed, buffer[0].getLength());
  (*result) *= (float)parmDepth;
  (*result) += (float)parmFrequency;
  DSP::buffer & interpolated = (*interpolator)(*result);
  (*result) = (*filter)(buffer[0], interpolated, (*result));

  calculateImpact(buffer[0], (*result));
}