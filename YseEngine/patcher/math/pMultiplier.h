#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pMultiplier : public pObject {
    public:
      pMultiplier();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}