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
      virtual void ResetData();
      virtual void RequestData();

      pHandle * AddObject(const char * type);
      
      void Connect(pHandle * from, int pinOut, pHandle * to, int pinIn);
      void Disconnect(pHandle * to, int pinIn);

      pHandle * GetOutputHandle(unsigned int output);

      std::vector<YSE::DSP::buffer>  output;

      aBool controlledBySound;
      std::atomic<patcher*> head;

    private:
      pHandle * CreateObject(const char * type);
      void DeleteObject(pHandle* handle);
      pHandle * AddOutput(PIN_TYPE type);

      std::map<pHandle*, pObject*> objects;
      std::vector<pHandle*> outputObjects;
    };

  }
}