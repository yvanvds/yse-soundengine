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

YSE::DSP::dspObject::dspObject() : next(NULL), previous(NULL), _bypass(false), calledfrom(NULL) {
}

YSE::DSP::dspObject::~dspObject() {
  if (previous != NULL) previous->next = next;
  if (next != NULL) next->previous = previous;
  if (calledfrom) *calledfrom = NULL;
}