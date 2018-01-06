#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pMidiToFrequency : public pObject {
    public:
      pMidiToFrequency();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();
    };

  }
}