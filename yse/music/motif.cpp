/*
  ==============================================================================

    motif.cpp
    Created: 11 Apr 2015 1:04:24pm
    Author:  yvan

  ==============================================================================
*/

#include "motif.hpp"
#include "pNote.hpp"
#include <algorithm>

YSE::MUSIC::motif & YSE::MUSIC::motif::add(pNote & note) {
  notes.emplace_back(note);
  return *this;
}

Bool noteCompare(const YSE::MUSIC::pNote & a, const YSE::MUSIC::pNote & b) {
  return a.getPosition() < b.getPosition();
}

YSE::MUSIC::motif & YSE::MUSIC::motif::addPart(const motif & other, UInt begin, UInt count) {
  assert(begin < notes.size() - 1);
  UInt end = begin + count;
  assert(end <= notes.size());
  Flt offset = notes[begin].getPosition();
  for (; begin < end; begin++) {
    notes.emplace_back(other.notes[begin]);
    notes.back().setPosition(notes.back().getPosition() - offset);
  }
  setLength();
  return *this;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::sort() {
  std::sort(notes.begin(), notes.end(), noteCompare);
  return *this;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::clear() {
  notes.clear();
  return *this;
}

UInt YSE::MUSIC::motif::getNotes(Flt startPos, Flt endPos, std::vector<MUSIC::pNote>::iterator & firstElement) {
  std::vector<pNote>::iterator low, high;
  low  = std::lower_bound(notes.begin(), notes.end(), pNote(note(), startPos), noteCompare);
  // nothing in range?
  if (low == notes.end()) return 0;

  high = std::upper_bound(notes.begin(), notes.end(), pNote(note(), endPos  ), noteCompare);
  
  // set pointer to first element
  firstElement = low;
  
  // return number of notes in range
  return std::distance(low, high);
}

YSE::MUSIC::motif & YSE::MUSIC::motif::setLength(Flt length) {
  this->length = length;
  return *this;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::setLength() {
  if (notes.empty()) {
    length = 0;
  }
  else {
    length = notes.back().getPosition() + notes.back().getLength();
  }
  return *this;
}

Flt YSE::MUSIC::motif::timeUntilNextNote(Flt pos) {
  std::vector<pNote>::iterator next;
  next = std::lower_bound(notes.begin(), notes.end(), pNote(note(), pos), noteCompare);
  if (next == notes.end()) return -1;
  return next->getPosition() - pos;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::transpose(Flt pitch) {
  // don't transpose an empty motif!
  assert(notes.size());

  Flt current = notes[0].getPitch();
  Flt diff = pitch - current;

  FOREACH(notes) {
    notes[i].setPitch(notes[i].getPitch() + diff);
  }

  return *this;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::setScale(const scale & startingpoints) {
  possibleFirstNotes = startingpoints;
  return *this;
}

const YSE::MUSIC::scale & YSE::MUSIC::motif::getScale() {
  return possibleFirstNotes;
}