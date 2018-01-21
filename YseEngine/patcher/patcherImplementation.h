#pragma once
#include "headers/defines.hpp"
#include "pObject.h"
#include "pHandle.hpp"
#include "dsp\buffer.hpp"
#include "patcher.hpp"
#include <map>

namespace YSE {
  namespace PATCHER {
    class pOutput;

    class patcherImplementation : public pObject {
    public:
      patcherImplementation(int mainOutputs, patcher * head);
      ~patcherImplementation();

      virtual const char * Type() const;
      virtual void ResetDSP();
      virtual void Calculate();

      virtual void SetParam(unsigned int pos, float value) {}
      virtual void SetMessage(const std::string & message, float value) {}

      pHandle * CreateObject(const char * type);
      void DeleteObject(pHandle * obj);
      
      void Connect(pHandle * from, int outlet, pHandle * to, int inlet);
      void Disconnect(pHandle * from, int outlet, pHandle * to, int inlet);

      std::vector<YSE::DSP::buffer>  output;

      aBool controlledBySound;
      std::atomic<patcher*> head;

    private:

      std::map<pHandle*, pObject*> objects;
    };

  }
}