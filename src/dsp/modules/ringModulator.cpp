#include "stdafx.h"
#include "ringModulator.hpp"
#include "../implementations.h"

YSE::DSP::ringModulator::ringModulator() : impl(new ringModImpl) {}
YSE::DSP::ringModulator::~ringModulator() {   delete impl;        }

YSE::DSP::ringModulator& YSE::DSP::ringModulator::frequency(Flt value) {
  impl->frequency.store(value);
  return *this;
}

Flt YSE::DSP::ringModulator::frequency() {
  return impl->frequency;
}

YSE::DSP::ringModulator& YSE::DSP::ringModulator::level(Flt value) {
  impl->level.store(value);
  return *this;
}

Flt YSE::DSP::ringModulator::level() {
  return impl->level;
}

void YSE::DSP::ringModulator::process(BUFFER buffer) {
	impl->extra.resize(buffer[0].getLength());
	
	// generate sine wave at wanted frequency
	SAMPLE sin = impl->sineGen(impl->frequency, buffer[0].getLength());

  Flt level = impl->level;
	for (UInt i = 0; i < buffer.size(); i++) {
		// make a copy of the original, this will add the dry sound to the output
		impl->extra = buffer[i];
		// adjust volume of dry sound
		impl->extra *= (Flt)(1-level);
		// combine input with sine to get a ring modulator effect
		buffer[i] *= sin;
		// adjust volume of wet sound
		buffer[i] *= level;

		// combine wet and dry sound 
		buffer[i] += impl->extra;
	}
}