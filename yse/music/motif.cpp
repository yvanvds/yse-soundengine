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

YSE::MUSIC::motif & YSE::MUSIC::motif::sort() {
  std::sort(notes.begin(), notes.end(), noteCompare);
  return *this;
}

YSE::MUSIC::motif & YSE::MUSIC::motif::clear() {
  notes.clear();
  return *this;
}

Int YSE::MUSIC::motif::getNotes(Flt startPos, Flt endPos, pNote * firstElement) {
  std::vector<pNote>::iterator low, high;
  low  = std::lower_bound(notes.begin(), notes.end(), pNote(note(), startPos), noteCompare);
  // nothing in range?
  if (low == notes.end()) return 0;

  high = std::upper_bound(notes.begin(), notes.end(), pNote(note(), endPos  ), noteCompare);
  
  // set pointer to first element
  if (firstElement != nullptr) {
    firstElement = &(*low);
  }

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