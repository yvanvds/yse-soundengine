#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pHighpass : public pObject {
    public:
      pHighpass();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}