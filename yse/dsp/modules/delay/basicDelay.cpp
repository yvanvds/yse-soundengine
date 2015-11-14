/*
  ==============================================================================

    basicDelay.cpp
    Created: 30 Sep 2015 4:40:17pm
    Author:  yvan

  ==============================================================================
*/

#include "basicDelay.hpp"

YSE::DSP::MODULES::basicDelay::basicDelay() : time0(0.f), time1(0.f), time2(0.f), gain0(0.f), gain1(0.f), gain2(0.f) {}

YSE::DSP::MODULES::basicDelay & YSE::DSP::MODULES::basicDelay::set(basicDelay::DELAY_NR nr, Flt time, Flt gain) {
  switch (nr) {
    case FIRST : time0.store(time); gain0.store(gain); break;
    case SECOND: time1.store(time); gain1.store(gain); break;
    case THIRD : time2.store(time); gain2.store(gain); break;
  }

  return *this;
}

Flt YSE::DSP::MODULES::basicDelay::time(basicDelay::DELAY_NR nr) {
  switch (nr) {
    case FIRST : return time0;
    case SECOND: return time1;
    case THIRD : return time2;
  }
}

Flt YSE::DSP::MODULES::basicDelay::gain(basicDelay::DELAY_NR nr) {
  switch (nr) {
  case FIRST : return gain0;
  case SECOND: return gain1;
  case THIRD : return gain2;
  }
}

void YSE::DSP::MODULES::basicDelay::create() {
  result.reset(new buffer);
  delayBuffer.reset(new delay(SAMPLERATE));
  reader.reset(new buffer);
  createPreFilter();
}

void YSE::DSP::MODULES::basicDelay::createPreFilter() {}
void YSE::DSP::MODULES::basicDelay::applyPreFilter(DSP::buffer & buffer) {}

void YSE::DSP::MODULES::basicDelay::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();

  if (buffer[0].getLength() != result->getLength()) {
    result->resize(buffer[0].getLength());
    reader->resize(buffer[0].getLength());
  }

  (*result) = buffer[0];
  applyPreFilter(buffer[0]);

  delayBuffer->process(buffer[0]);
  Int delayCount = 1; // the original signal

  // add delays to signal
  if (gain0 > 0) {
    delayBuffer->read(*reader, time0);
    (*reader) *= gain0;
    (*result) += (*reader);
    delayCount++;
  }

  if (gain1 > 0) {
    delayBuffer->read(*reader, time1);
    (*reader) *= gain1;
    (*result) += (*reader);
    delayCount++;
  }

  if (gain2 > 0) {
    delayBuffer->read(*reader, time2);
    (*reader) *= gain2;
    (*result) += (*reader);
    delayCount++;
  }

  // adjust total gain
  (*result) *= (1.f / delayCount);
  calculateImpact(buffer[0], (*result));
}
