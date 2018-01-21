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

    void SetData(unsigned int inlet, float value);
    void SetParam(unsigned int pos, float value);

    int GetInputs();
    int GetOutputs();

    bool IsDSPInput(unsigned int inlet);
    YSE::OUT_TYPE OutputDataType(unsigned int pin);

  private:
    PATCHER::pObject * object;
    friend class YSE::PATCHER::patcherImplementation;
  };


}