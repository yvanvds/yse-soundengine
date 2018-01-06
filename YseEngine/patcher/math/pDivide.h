#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pDivide : public pObject {
    public:
      pDivide();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}