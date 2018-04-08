#pragma once
#include "headers/defines.hpp"
#include "pObject.h"
#include "pHandle.hpp"
#include "dsp\buffer.hpp"
#include "patcher.hpp"
#include <map>
#include <mutex>

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

      virtual void SetMessage(const std::string & message, float value) {}

      pHandle * CreateObject(const std::string & type, const std::string & args);
      void DeleteObject(pHandle * obj);
      void Clear();
      
      void Connect(pHandle * from, int outlet, pHandle * to, int inlet);
      void Disconnect(pHandle * from, int outlet, pHandle * to, int inlet);

      std::string DumpJSON();
      void ParseJSON(const std::string & content);

      unsigned int Objects();
      YSE::pHandle * GetHandleFromList(unsigned int obj);
      YSE::pHandle * GetHandleFromID(unsigned int objID);

      std::vector<YSE::DSP::buffer>  output;

      aBool controlledBySound;
      std::atomic<patcher*> head;

    private:
      std::mutex mtx;
      bool fileHandlerActive;
      std::map<pHandle*, pObject*> objects;

    };

  }
}