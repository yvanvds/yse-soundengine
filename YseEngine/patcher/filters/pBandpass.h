#pragma once
#include "..\pObject.h"
#include "dsp\filters.hpp"

namespace YSE {
  namespace PATCHER {

    class pBandpass : public pObject {
    public:
      pBandpass();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      YSE::DSP::bandPass filter;
    };
  }
}