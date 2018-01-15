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

    YSE::pHandle * CreateObject(const char * type);
    void DeleteObject(YSE::pHandle * obj);

    void Connect(YSE::pHandle * from, int pinOut, YSE::pHandle * to, int pinIn);
    void Disconnect(YSE::pHandle * to, int pinIn);

    YSE::pHandle * GetOutputHandle(unsigned int output);

    static bool IsValidObject(const char * type);

  private: 
    PATCHER::patcherImplementation * pimpl;
    
    friend class YSE::sound;
  };

}