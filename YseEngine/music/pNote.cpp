/*
  ==============================================================================

    pNote.cpp
    Created: 11 Apr 2015 12:38:20pm
    Author:  yvan

  ==============================================================================
*/

#include "pNote.hpp"

YSE::MUSIC::pNote::pNote(Flt position, Flt pitch, Flt volume, Flt length, Int channel)
  : note(pitch, volume, length, channel), position(position) {}

YSE::MUSIC::pNote::pNote(const note & object, Flt position) 
: note(object), position(position) {}

YSE::MUSIC::pNote & YSE::MUSIC::pNote::setPosition(Flt value) {
  position = value;
  return *this;
}

Flt YSE::MUSIC::pNote::getPosition() const {
  return position;
}

