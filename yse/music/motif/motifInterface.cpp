/*
  ==============================================================================

    motifInterface.cpp
    Created: 14 Apr 2015 6:18:45pm
    Author:  yvan

  ==============================================================================
*/

#include "../../internalHeaders.h"
#include "motifInterface.hpp"
#include <algorithm>

Bool noteCompare(const YSE::MUSIC::pNote & a, const YSE::MUSIC::pNote & b) {
  return a.getPosition() < b.getPosition();
}

YSE::MOTIF::interfaceObject::interfaceObject() : length(0) {
  pimpl = MOTIF::Manager().addImplementation(this);
}

YSE::MOTIF::interfaceObject::~interfaceObject() {
  pimpl->removeInterface();
}

YSE::motif & YSE::MOTIF::interfaceObject::add(const MUSIC::pNote & note) {
  notes.emplace_back(note);
  sort();

  messageObject m;
  m.ID = ADD;
  m.note.position = note.getPosition();
  m.note.pitch = note.getPitch();
  m.note.length = note.getLength();
  m.note.volume = note.getVolume();
  m.note.channel = note.getChannel();
  pimpl->sendMessage(m);

  return *this;
}

void YSE::MOTIF::interfaceObject::sort() {
  std::sort(notes.begin(), notes.end(), noteCompare);
}

YSE::motif & YSE::MOTIF::interfaceObject::clear() {
  notes.clear();
  messageObject m;
  m.ID = CLEAR;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::MOTIF::interfaceObject::setLength(Flt length) {
  this->length = length;
  
  messageObject m;
  m.ID = LENGTH;
  m.floatValue = length;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::MOTIF::interfaceObject::setLength() {
  if (notes.empty()) {
    length = 0;
  }
  else {
    length = notes.back().getPosition() + notes.back().getLength();
  }

  messageObject m;
  m.ID = LENGTH;
  m.floatValue = length;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::MOTIF::interfaceObject::transpose(Flt pitch) {
  FOREACH(notes) {
    notes[i].setPitch(notes[i].getPitch() + pitch);
  }

  messageObject m;
  m.ID = TRANSPOSE;
  m.floatValue = pitch;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::MOTIF::interfaceObject::setFirstPitch(const scale & validPitches) {
  messageObject m;
  m.ID = FIRST_PITCH;
  m.ptr = validPitches.pimpl;
  pimpl->sendMessage(m);
  return *this;
}

YSE::MUSIC::pNote & YSE::MOTIF::interfaceObject::operator[](UInt pos) {
  return notes[pos];
}