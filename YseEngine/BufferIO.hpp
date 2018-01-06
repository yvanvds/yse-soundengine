#ifndef BUFFERIO_H_INCLUDED
#define BUFFERIO_H_INCLUDED

#include "headers/defines.hpp"

namespace YSE {

  class API BufferIO {
  public:
    BufferIO(bool storeCopy = false);

    void SetActive(bool value);
    bool GetActive();

    bool BufferNameExists(const char * ID);
    bool BufferExists(char * buffer);

    bool AddBuffer(const char * ID, char * buffer, int length);
    
    bool RemoveBufferByName(const char * ID);
    bool RemoveBuffer(char * buffer);

    ~BufferIO();

  private:
    bool active;
    bool storeCopy;
  };

}

#endif
