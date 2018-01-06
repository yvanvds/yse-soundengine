#pragma once
#include "patcher/pHandle.hpp"

namespace YSE {
  class sound;
  namespace PATCHER {
    class patcherImplementation;
  }
  

  class API patcher {
  public:
    patcher();
    ~patcher();

    void create(int mainOutputs);

    YSE::pHandle * AddObject(const char * type);

    void Connect(YSE::pHandle * from, int pinOut, YSE::pHandle * to, int pinIn);
    void Disconnect(YSE::pHandle * to, int pinIn);

    YSE::pHandle * GetOutputHandle(unsigned int output);

  private: 
    PATCHER::patcherImplementation * pimpl;
    
    friend class YSE::sound;
  };

}