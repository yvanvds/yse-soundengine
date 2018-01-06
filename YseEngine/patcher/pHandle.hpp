#pragma once
#include "pObject.h"
#include "headers/defines.hpp"

namespace YSE {
  namespace PATCHER {
    class patcherImplementation;
  }

  class API pHandle {
  public:
    pHandle(PATCHER::pObject * obj);
    const char * Type() const;

    bool SetData(unsigned int pin, bool value);
    bool SetData(unsigned int pin, int value);
    bool SetData(unsigned int pin, float value);
    bool SetData(unsigned int pin, const char * value);

  private:
    PATCHER::pObject * object;
    friend class YSE::PATCHER::patcherImplementation;
  };


}