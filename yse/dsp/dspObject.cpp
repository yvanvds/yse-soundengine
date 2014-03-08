/*
  ==============================================================================

    source.cpp
    Created: 31 Jan 2014 2:53:05pm
    Author:  yvan

  ==============================================================================
*/

#include "dspObject.hpp"

YSE::DSP::dspSourceObject::dspSourceObject(Int buffers) {
  buffer.resize(buffers);
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

YSE::DSP::dspObject::dspObject() : next(nullptr), previous(nullptr), _bypass(false), calledfrom(nullptr) {
}

YSE::DSP::dspObject::~dspObject() {
  if (previous != nullptr) previous->next = next;
  if (next != nullptr) next->previous = previous;
  if (calledfrom) *calledfrom = nullptr;
}