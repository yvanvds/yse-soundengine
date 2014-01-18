#include "stdafx.h"
#include "dsp.hpp"

YSE::DSP::dspSource::dspSource(Int buffers) {
	buffer.resize(buffers);
}

void YSE::DSP::dsp::link(YSE::DSP::dsp& next) {
  next.next = this->next;
  next.previous = this;
  if (next.next) next.next->previous = &next;
  this->next = &next;
}

YSE::DSP::dsp * YSE::DSP::dsp::link() {
  return this->next;
}

YSE::DSP::dsp::dsp() {
  next = NULL;
  previous = NULL;
  _bypass = false;
  calledfrom = NULL;
}

YSE::DSP::dsp::~dsp() {
  if (previous != NULL) previous->next = next;
  if (next != NULL) next->previous = previous;
  if (calledfrom) *calledfrom = NULL;
}