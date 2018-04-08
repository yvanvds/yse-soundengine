#pragma once
#include "dsp/buffer.hpp"
#include "headers\enums.hpp"
#include <vector>
#include "utils\json.hpp"

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct inlet;

    

    struct outlet {
      outlet(pObject * obj, OUT_TYPE type);
      ~outlet();

      inline OUT_TYPE Type() const { return type; } 

      void SendBang();
      void SendFloat(float value);
      void SendInt(int value);
      void SendList(const std::string & value);
      void SendBuffer(DSP::buffer * value);

      void Connect(inlet * in);
      void Disconnect(inlet * in);

      void DumpJSON(nlohmann::json::value_type & json);

      unsigned int GetConnections();
      unsigned int GetTarget(unsigned int connection);
      unsigned int GetTargetInlet(unsigned int connection);

    private:
      pObject * obj;
      OUT_TYPE type;

      std::vector<inlet*> connections;
    };

  }
}