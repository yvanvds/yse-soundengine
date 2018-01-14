#pragma once
#include "..\pObject.h"
#include "dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    class pLowpass : public pObject {
    public:
      pLowpass();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::lowPass filter;
    };
  }
}