#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {
    /* 
      An object with one input and one output.
      It can be used to pass data out of a patcher
    */
    class pOutput : public pObject {
    public:
      pOutput();

      bool Setup(PIN_TYPE type);

      virtual const char * Type() const;

      virtual void RequestData();

      YSE::DSP::buffer * GetBuffer(int pin);

      static pObject * Create();

    private:
      bool ready;
      PIN_TYPE type;
    };

  }
}
