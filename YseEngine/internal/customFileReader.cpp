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
      bool(*openPtr)(const char * filename, long long * filesize, void ** fileHandle);
      void(*closePtr)(void * fileHandle);
      long long(*readPtr)(void * destBuffer, long long maxBytesToRead, void * fileHandle);
      long long(*getPosPtr)(void * fileHandle);
      bool(*fileExists)(const char * filename);
      long long(*lengthPtr)(void * fileHandle);
      long long(*seekPtr)(long long offset, int whence, void * fileHandle);

      SF_VIRTUAL_IO vio;
    }
  }
}

bool YSE::INTERNAL::customFileReader::Open(const char * filename, long long * filesize, void ** fileHandle) {
  if (CALLBACK::openPtr != nullptr) {
    return CALLBACK::openPtr(filename, filesize, fileHandle);
  }
  return false;
}

void YSE::INTERNAL::customFileReader::Close(void * fileHandle) {
  if (CALLBACK::closePtr != nullptr) {
    CALLBACK::closePtr(fileHandle);
  }
}

void YSE::INTERNAL::customFileReader::UpdateVIO() {
  CALLBACK::vio.get_filelen = CALLBACK::lengthPtr;
  CALLBACK::vio.read = CALLBACK::readPtr;
  CALLBACK::vio.seek = CALLBACK::seekPtr;
  CALLBACK::vio.tell = CALLBACK::getPosPtr;
  CALLBACK::vio.write = NULL;
}

void YSE::INTERNAL::customFileReader::ResetVIO() {
  CALLBACK::vio.get_filelen = NULL;
  CALLBACK::vio.read = NULL;
  CALLBACK::vio.seek = NULL;
  CALLBACK::vio.tell = NULL;
  CALLBACK::vio.write = NULL;

  CALLBACK::closePtr = nullptr;
  CALLBACK::fileExists = nullptr;
  CALLBACK::getPosPtr = nullptr;
  CALLBACK::lengthPtr = nullptr;
  CALLBACK::openPtr = nullptr;
  CALLBACK::readPtr = nullptr;
  CALLBACK::seekPtr = nullptr;
}

SF_VIRTUAL_IO & YSE::INTERNAL::customFileReader::GetVIO() {
  return CALLBACK::vio;
}