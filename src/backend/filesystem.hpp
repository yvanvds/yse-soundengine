#pragma once
#include <string>
#include "../headers/enums.hpp"


namespace YSE {
  class filesysImpl; // for internal use

  class API filesystem {
  public:
	  filesystem& open  (Bool(*funcPtr)(const char* filename, UInt  * filesize,  void ** handle, void ** userdata));
	  filesystem& close (void(*funcPtr)(                                         void *  handle, void *  userdata));
    filesystem& length(I64 (*funcPtr)(                                         void *  handle, void *  userdata));
    filesystem& seek  (I64 (*funcPtr)(FILEPOINT point     , UInt offset     ,  void *  handle, void *  userdata));
    filesystem& read  (I64 (*funcPtr)(void * buffer, UInt bytesRequested    ,  void *  handle, void *  userdata));
    filesystem& write (I64 (*funcPtr)(const void * buffer, UInt bytesToWrite,  void *  handle, void *  userdata));
    filesystem& tell  (I64 (*funcPtr)(                                         void *  handle, void *  userdata));

    filesystem& active(Bool v); Bool active();

    filesystem();
  private:
    filesysImpl * pimpl;
  };

  API filesystem& IO();
}
