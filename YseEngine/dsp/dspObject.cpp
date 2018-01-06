/*
  ==============================================================================

    source.cpp
    Created: 31 Jan 2014 2:53:05pm
    Author:  yvan

  ==============================================================================
*/

#include "dspObject.hpp"

YSE::DSP::dspSourceObject::dspSourceObject(Int buffers) {
  samples.resize(buffers);
}

void YSE::DSP::dspObject::link(YSE::DSP::dspObject& next) {
  next.next = this->next;
  next.previous = this;
  if (next.next) next.next->previous = &next;
  this->next = &next;
}

YSE::DSP::dspObject * YSE::DSP::dspObject::link() {
  return this->next;
}

YSE::DSP::dspObject::dspObject() 
  : calledfrom(nullptr), 
	next(nullptr),
    previous(nullptr), 
    _bypass(false), 
    _needsCreate(true),
	_impact(1.f),
    _lfoType(LFO_NONE),
	_lfoFrequency(0.f)
    {
  lfoOsc.reset(new lfo);
  invertedImpact.reset(new inverter);
}

YSE::DSP::dspObject::~dspObject() {
  if (previous != nullptr) previous->next = next;
  if (next != nullptr) next->previous = previous;
  if (calledfrom) *calledfrom = nullptr;
}

void YSE::DSP::dspObject::createIfNeeded() {
  if (_needsCreate) {
    create();
    _needsCreate = false;
  }
}

void YSE::DSP::dspObject::calculateImpact(buffer & in, buffer & filtered) {
  buffer & lfoImpact = (*lfoOsc)(_lfoType, _lfoFrequency);
  lfoImpact *= _impact;
  buffer & inImpact = (*invertedImpact)(lfoImpact, true);
  in *= inImpact;
  filtered *= lfoImpact;
  in += filtered;
}

YSE::DSP::buffer & YSE::DSP::dspObject::getLFO() {
  return (*lfoOsc)(_lfoType, _lfoFrequency);
}