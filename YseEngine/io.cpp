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

YSE::io & YSE::io::open(bool(*funcPtr)(const char * filename, long long * filesize, void ** fileHandle)) {
  INTERNAL::CALLBACK::openPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::close(void(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::closePtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::read(long long(*funcPtr)(void * destBuffer, long long maxBytesToRead, void * fileHandle)) {
  INTERNAL::CALLBACK::readPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::getPosition(long long(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::getPosPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::fileExists(bool(*funcPtr)(const char * filename)) {
  INTERNAL::CALLBACK::fileExists = funcPtr;
  return *this;
}

YSE::io & YSE::io::length(long long(*funcPtr)(void * fileHandle)) {
  INTERNAL::CALLBACK::lengthPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::seek(long long(*funcPtr)(long long offset, int whence, void * fileHandle)) {
  INTERNAL::CALLBACK::seekPtr = funcPtr;
  return *this;
}

YSE::io & YSE::io::setActive(bool value) {
  active.store(value);
  
  if (value) INTERNAL::customFileReader::UpdateVIO();
  else       INTERNAL::customFileReader::ResetVIO();
  
  return *this;
}

Bool YSE::io::getActive() {
  return active;
}
