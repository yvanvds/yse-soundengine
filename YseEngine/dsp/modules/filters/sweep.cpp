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
  } // namespace DSP
} // namespace YSE

YSE::DSP::MODULES::sweepFilter::sweepFilter(SHAPE shape)
  : parmSpeed(0), parmDepth(0), parmFrequency(0), shape(shape) {}

YSE::DSP::MODULES::sweepFilter& YSE::DSP::MODULES::sweepFilter::frequency(Int value) {
  Clamp(value, 0, 100);
  parmFrequency.store(value);
  return *this;
}

Int YSE::DSP::MODULES::sweepFilter::frequency() {
  return parmFrequency;
}

YSE::DSP::MODULES::sweepFilter& YSE::DSP::MODULES::sweepFilter::depth(Int value) {
  Clamp(value, 0, 100);
  parmDepth.store(value);
  return *this;
}

Int YSE::DSP::MODULES::sweepFilter::depth() {
  return parmDepth;
}

YSE::DSP::MODULES::sweepFilter& YSE::DSP::MODULES::sweepFilter::speed(Flt value) {
  parmSpeed.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::sweepFilter::speed() {
  return parmSpeed;
}

void YSE::DSP::MODULES::sweepFilter::create() {
  table.reset(new wavetable);

  switch (shape) {
  case TRIANGLE:
    table->createTriangle(8, SAMPLERATE);
    break;
  case SAW:
    table->createSaw(8, SAMPLERATE);
    break;
  case SQUARE:
    table->createSquare(8, SAMPLERATE);
    break;
  }

  osc.reset(new oscillator);
  osc->initialize(*table);
  interpolator.reset(new DSP::interpolate4);
  // Per-channel vcf filters are sized lazily in process(); each is given the
  // same resonance the mono filter used.

  if (!mtofTable.getLength()) {
    mtofTable.resize(130);
    Flt* t = mtofTable.getPtr();
    for (int i = 0; i < 130; i++) {
      *t++ = MidiToFreq((float)i);
    }
  }

  interpolator->source(mtofTable);
}

void YSE::DSP::MODULES::sweepFilter::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Grow/shrink per-channel filters to the channel count. New filters get the
  // same resonance the mono path used. Allocation-free once the count is stable.
  filter.ensure(buffer.size(), [](vcf& f) { f.sharpness(2); });

  // The sweep LFO and its frequency mapping are identical across channels, so
  // compute the cutoff control signal once per block.
  if (buffer[0].getLength() != control.getLength()) {
    control.resize(buffer[0].getLength());
  }
  control = (*osc)(parmSpeed, buffer[0].getLength());
  control *= (float)parmDepth;
  control += (float)parmFrequency;
  DSP::buffer& interpolated = (*interpolator)(control);

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
    }
    result = filter[ch](buffer[ch], interpolated).real();
    calculateImpact(buffer[ch], result);
  }
}