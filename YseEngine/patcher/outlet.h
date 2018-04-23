#pragma once
#include "../dsp/buffer.hpp"
#include "../headers\enums.hpp"
#include <vector>
#include "../utils\json.hpp"

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct inlet;

    

    struct outlet {
      outlet(OUT_TYPE type);
      ~outlet();

      inline OUT_TYPE Type() const { return type; } 

      void SendBang(THREAD thread);
      void SendFloat(float value, THREAD thread);
      void SendInt(int value, THREAD thread);
      void SendList(const std::string & value, THREAD thread);
      void SendBuffer(DSP::buffer * value, THREAD thread);

      void Connect(inlet * in);
      void Disconnect(inlet * in);

      void DumpJSON(nlohmann::json::value_type & json);

      unsigned int GetConnections();
      unsigned int GetTarget(unsigned int connection);
      unsigned int GetTargetInlet(unsigned int connection);

    private:
      OUT_TYPE type;

      std::vector<inlet*> connections;
    };

  }
}