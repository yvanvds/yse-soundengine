#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pFrequencyToMidi : public pObject {
    public:
      pFrequencyToMidi();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();
    };

  }
}