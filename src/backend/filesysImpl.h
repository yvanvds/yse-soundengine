#pragma once
#include "sndfile.h"
#include "headers/enums.hpp"
#include <string>

namespace YSE {
  struct fileData {
    std::string filename;
    UInt filesize;
    void * handle;
    void * userdata;
  };

  class filesysImpl {
  public:
    filesysImpl();
    SF_VIRTUAL_IO * operator()();

    Bool _active;
    I64(*lengthPtr)(void * handle, void * userdata);
    I64(*seekPtr  )(FILEPOINT point, UInt offset, void * handle, void * userdata);
    I64(*readPtr  )(void * buffer, UInt bytesRequested, void * handle, void * userdata); // should return the actual amount of bytes read
    I64(*writePtr )(const void * buffer, UInt bytesToWrite, void * handle, void * userdata);
    I64(*tellPtr  )(void * handle, void * userdata);
	  Bool(*openPtr  )(const char * filename, UInt  * filesize, void ** handle, void ** userdata);
	  void(*closePtr )(void * handle, void * userdata);

    SF_VIRTUAL_IO vio;
    
    // sndfile wrappers
    static Bool open(const char * filename, UInt  * filesize, void ** handle, void ** userdata);
    static void close(void * handle, void * userdata);
    static sf_count_t length(void *user_data);
    static sf_count_t seek(sf_count_t offset, int whence, void *user_data);
    static sf_count_t read(void *ptr, sf_count_t count, void *user_data);
    static sf_count_t write(const void *ptr, sf_count_t count, void *user_data);
    static sf_count_t tell(void *user_data);

  };

  extern filesysImpl IOImpl;
}