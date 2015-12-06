/*
  ==============================================================================

    customFileReader.cpp
    Created: 23 Apr 2014 5:22:57pm
    Author:  yvan

  ==============================================================================
*/

#include "customFileReader.h"

namespace YSE {
  namespace INTERNAL {
    namespace CALLBACK {
      Bool(*openPtr)(const char * filename, I64 * filesize, void ** fileHandle);
      void(*closePtr)(void * fileHandle);
      Bool(*endOfFilePtr)(void * fileHandle);
      Int(*readPtr)(void * destBuffer, Int maxBytesToRead, void * fileHandle);
      I64(*getPosPtr)(void * fileHandle);
      Bool(*setPosPtr)(I64 newPosition, void * fileHandle);
      Bool(*fileExists)(const char * filename);
    }
  }
}


YSE::INTERNAL::customFileReader::customFileReader() : fileSize(0), fileHandle(nullptr) {}

YSE::INTERNAL::customFileReader::~customFileReader() {
  if (fileHandle != nullptr) {
    CALLBACK::closePtr(fileHandle);
  }
}

Bool YSE::INTERNAL::customFileReader::create(const char * filename) {
  return CALLBACK::openPtr(filename, &fileSize, &fileHandle);
}

int64 YSE::INTERNAL::customFileReader::getTotalLength() {
  return fileSize;
}

Bool YSE::INTERNAL::customFileReader::isExhausted() {
  return CALLBACK::endOfFilePtr(fileHandle);
}

Int YSE::INTERNAL::customFileReader::read(void * destBuffer, Int maxBytesToRead){
  return CALLBACK::readPtr(destBuffer, maxBytesToRead, fileHandle);
}

I64 YSE::INTERNAL::customFileReader::getPosition() {
  return CALLBACK::getPosPtr(fileHandle);
}

Bool YSE::INTERNAL::customFileReader::setPosition(I64 newPosition) {
  return CALLBACK::setPosPtr(newPosition, fileHandle);
}
