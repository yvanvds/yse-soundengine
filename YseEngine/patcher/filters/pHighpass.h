#pragma once
#include "..\pObject.h"
#include "dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    class pHighpass : public pObject {
    public:
      pHighpass();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::highPass filter;
    };
  }
}