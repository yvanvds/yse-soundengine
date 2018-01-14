#pragma once
#include "..\pObject.h"
#include "dsp/ramp.hpp"

namespace YSE {
  namespace PATCHER {

    class pLine : public pObject {
    public:
      pLine();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::ramp ramp;
    };
  }
}