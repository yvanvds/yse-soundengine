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

YSE::motif::motif() : length(0) {
  pimpl = MOTIF::Manager().addImplementation(this);
}

YSE::motif::~motif() {
  pimpl->removeInterface();
}

YSE::motif & YSE::motif::add(const MUSIC::pNote & note) {
  notes.emplace_back(note);
  sort();

  MOTIF::messageObject m;
  m.ID = MOTIF::ADD;
  m.note.position = note.getPosition();
  m.note.pitch = note.getPitch();
  m.note.length = note.getLength();
  m.note.volume = note.getVolume();
  m.note.channel = note.getChannel();
  pimpl->sendMessage(m);

  return *this;
}

void YSE::motif::sort() {
  std::sort(notes.begin(), notes.end(), noteCompare);
}

YSE::motif & YSE::motif::clear() {
  notes.clear();
  MOTIF::messageObject m;
  m.ID = MOTIF::CLEAR;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::motif::setLength(Flt length) {
  this->length = length;
  
  MOTIF::messageObject m;
  m.ID = MOTIF::LENGTH;
  m.floatValue = length;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::motif::setLength() {
  if (notes.empty()) {
    length = 0;
  }
  else {
    length = notes.back().getPosition() + notes.back().getLength();
  }

  MOTIF::messageObject m;
  m.ID = MOTIF::LENGTH;
  m.floatValue = length;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::motif::transpose(Flt pitch) {
  FOREACH(notes) {
    notes[i].setPitch(notes[i].getPitch() + pitch);
  }

  MOTIF::messageObject m;
  m.ID = MOTIF::TRANSPOSE;
  m.floatValue = pitch;
  pimpl->sendMessage(m);

  return *this;
}

YSE::motif & YSE::motif::setFirstPitch(const scale & validPitches) {
  MOTIF::messageObject m;
  m.ID = MOTIF::FIRST_PITCH;
  m.ptr = validPitches.pimpl;
  pimpl->sendMessage(m);
  return *this;
}

YSE::MUSIC::pNote & YSE::motif::operator[](UInt pos) {
  return notes[pos];
}