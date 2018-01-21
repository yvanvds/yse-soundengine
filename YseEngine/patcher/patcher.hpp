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

    void Connect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet);
    void Disconnect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet);

    static bool IsValidObject(const char * type);

  private: 
    PATCHER::patcherImplementation * pimpl;
    
    friend class YSE::sound;
  };

}