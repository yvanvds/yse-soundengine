/*
  ==============================================================================

    io.cpp
    Created: 23 Apr 2014 7:33:47pm
    Author:  yvan

  ==============================================================================
*/

#include "io.hpp"
#include "internal/customFileReader.h"

YSE::io & YSE::IO() {
  static io fs;
  return fs;
}

YSE::io::io() : active(false) {}

YSE::io & YSE::io::open(Bool(*funcPtr)(const char * filename, I64 * filesize, void ** fileHandle)) {
  INTERNAL::CALLBACK::openPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::close(void(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::closePtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::endOfFile(Bool(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::endOfFilePtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::read(Int(*funcPtr)(void * destBuffer, Int maxBytesToRead, void * fileHandle)) {
  INTERNAL::CALLBACK::readPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::getPosition(I64(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::getPosPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::setPosition(Bool(*funcPtr)(I64 newPosition, void * fileHandle)) {
  INTERNAL::CALLBACK::setPosPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::fileExists(Bool(*funcPtr)(const char * filename)) {
  INTERNAL::CALLBACK::fileExists = funcPtr;
  return *this;
}

YSE::io & YSE::io::setActive(Bool value) {
  active.store(value);
  return *this;
}

Bool YSE::io::getActive() {
  return active;
}
