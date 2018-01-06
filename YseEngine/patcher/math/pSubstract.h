#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pSubstract : public pObject {
    public:
      pSubstract();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}