#include "stdafx.h"
#include "filesystem.hpp"
#include "sndfile.h"
#include "filesysImpl.h"

YSE::filesystem::filesystem() {
  pimpl = &IOImpl;
}

Bool YSE::filesystem::active() {
  return pimpl->_active;
}

YSE::filesystem& YSE::filesystem::active(Bool v) {
  if (!v) pimpl->_active = false;
  else {
    if ((pimpl->lengthPtr != 0) 
      && (pimpl->seekPtr != 0)
      && (pimpl->readPtr != 0)
      && (pimpl->writePtr != 0)
      && (pimpl->tellPtr != 0)
	    && (pimpl->openPtr != 0)
	    && (pimpl->closePtr != 0))
      pimpl->_active = true;
  }
  return *this;
}


YSE::filesystem& YSE::filesystem::open(Bool(*funcPtr)(const char * filename, UInt  * filesize,  void ** handle, void ** userdata)) {
  pimpl->openPtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::close(void(*funcPtr)(void *  handle, void *  userdata)) {
  pimpl->closePtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::length(I64(*funcPtr)(void *  handle, void *  userdata)) {
  pimpl->lengthPtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::seek(I64(*funcPtr)(FILEPOINT point, UInt offset,  void *  handle, void *  userdata)) {
  pimpl->seekPtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::read(I64(*funcPtr)(void * buffer, UInt bytesRequested,  void *  handle, void *  userdata)) {
  pimpl->readPtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::write(I64(*funcPtr)(const void * buffer, UInt bytesToWrite,  void *  handle, void *  userdata)) {
  pimpl->writePtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::filesystem::tell(I64(*funcPtr)(void *  handle, void *  userdata)) {
  pimpl->tellPtr = funcPtr;
  return *this;
}

YSE::filesystem& YSE::IO() {
  static filesystem f;
  return f;
}