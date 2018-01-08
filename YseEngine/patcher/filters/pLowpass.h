#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pLowpass : public pObject {
    public:
      pLowpass();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}