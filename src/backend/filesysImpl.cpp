#include "stdafx.h"
#include "filesysImpl.h"

YSE::filesysImpl YSE::IOImpl;

YSE::filesysImpl::filesysImpl() {
  _active = false;
  lengthPtr = 0;
  seekPtr = 0;
  readPtr = 0;
  writePtr = 0;
  tellPtr = 0;
  openPtr = 0;
  closePtr = 0;

  vio.get_filelen = length;
  vio.seek = seek;
  vio.read = read;
  vio.write = write;
  vio.tell = tell;
}

SF_VIRTUAL_IO * YSE::filesysImpl::operator()() {
  if (_active) return &vio;
  return NULL;
}

Bool YSE::filesysImpl::open(const char * filename, UInt  * filesize, void ** handle, void ** userdata) {
  return IOImpl.openPtr(filename, filesize, handle, userdata);
}

void YSE::filesysImpl::close(void * handle, void * userdata) {
  IOImpl.closePtr(handle, userdata);
}

sf_count_t YSE::filesysImpl::length(void *user_data) {
  fileData * f = (fileData*)user_data;
  return IOImpl.lengthPtr(f->handle, f->userdata);
}

sf_count_t YSE::filesysImpl::seek(sf_count_t offset, int whence, void *user_data) {
  FILEPOINT p;
  switch (whence) {
    case SEEK_SET: p = FP_START; break;
    case SEEK_CUR: p = FP_CURRENT; break;
    case SEEK_END: p = FP_END; break;
  }
  fileData * f = (fileData*)user_data;
  return IOImpl.seekPtr(p, (UInt)offset, f->handle, f->userdata);
}

sf_count_t YSE::filesysImpl::read(void *ptr, sf_count_t count, void *user_data) {
  fileData * f = (fileData*)user_data;
  return IOImpl.readPtr(ptr, (UInt)count, f->handle, f->userdata);
}

sf_count_t YSE::filesysImpl::write(const void *ptr, sf_count_t count, void *user_data) {
  fileData * f = (fileData*)user_data;
  return IOImpl.writePtr(ptr, (UInt)count, f->handle, f->userdata);
}

sf_count_t YSE::filesysImpl::tell(void * user_data) {
  fileData * f = (fileData*)user_data;
  return IOImpl.tellPtr(f->handle, f->userdata);
}


