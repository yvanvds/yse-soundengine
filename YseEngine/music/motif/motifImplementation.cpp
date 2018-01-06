/*
  ==============================================================================

    motifImplementation.cpp
    Created: 14 Apr 2015 6:19:01pm
    Author:  yvan

  ==============================================================================
*/

#include "motif.hpp"
#include "../../internalHeaders.h"
#include <algorithm>

Bool noteCompare2(const YSE::MUSIC::pNote & a, const YSE::MUSIC::pNote & b) {
  return a.getPosition() < b.getPosition();
}

YSE::MOTIF::implementationObject::implementationObject(motif * head)
: head(head) , validPitches(nullptr) {}

YSE::MOTIF::implementationObject::~implementationObject() {
  if (head.load() != nullptr) {
    head.load()->pimpl = nullptr;
  }
}

bool YSE::MOTIF::implementationObject::update() {
  if (head.load() == nullptr) return false;

  // parse messages
  messageObject message;
  needsSorting = false;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }

  if (needsSorting) {
    sort();
  }
  return true;
}

void YSE::MOTIF::implementationObject::removeInterface() {
  head.store(nullptr);
}

void YSE::MOTIF::implementationObject::parseMessage(const messageObject & message) {
  switch (message.ID) {
  case ADD:
    notes.emplace_back(message.note.position, message.note.pitch, message.note.volume, message.note.length, message.note.channel);
    needsSorting = true;
    break;
  case CLEAR:
    notes.clear();
    break;
  case LENGTH:
    length = message.floatValue;
    break;
  case TRANSPOSE:
    FOREACH(notes) {
      notes[i].setPitch(notes[i].getPitch() + message.floatValue);
    }
    break;
  case FIRST_PITCH:
    validPitches = (SCALE::implementationObject*) message.ptr;
    break;
  }
}

void YSE::MOTIF::implementationObject::sort() {
  std::sort(notes.begin(), notes.end(), noteCompare2);
}

YSE::MUSIC::pNote & YSE::MOTIF::implementationObject::getNote(UInt pos) {
  return notes[pos];
}

YSE::SCALE::implementationObject * YSE::MOTIF::implementationObject::getValidPitches() {
  return validPitches;
}