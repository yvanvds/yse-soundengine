#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    class pAdd : public pObject {
    public:
      pAdd();

      virtual const char * Type() const;

      virtual void RequestData();

      static pObject * Create();

    private:
      DSP::buffer output;
    };
  }
}