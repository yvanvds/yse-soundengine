#pragma once
#include "../dsp/buffer.hpp"
#include "../headers/enums.hpp"
#include "../utils/json.hpp"
#include <string>
#include <vector>

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
	  void SendMessage(const std::string& value, THREAD thread);
      void SendBuffer(DSP::buffer * value, THREAD thread);

      void Connect(inlet * in);
      void Disconnect(inlet * in);

      void DumpJSON(nlohmann::json::value_type & json);

      unsigned int GetConnections();
      unsigned int GetTarget(unsigned int connection);
      unsigned int GetTargetInlet(unsigned int connection);

      // Documentation surface — see inlet::SetDoc for usage notes.
      void SetDoc(const std::string & label,
                  const std::string & doc,
                  const std::string & range);
      const std::string & GetDocLabel() const { return docLabel; }
      const std::string & GetDocDescription() const { return docDescription; }
      const std::string & GetRange() const { return docRange; }

    private:
      OUT_TYPE type;

      std::vector<inlet*> connections;

      std::string docLabel;
      std::string docDescription;
      std::string docRange;
    };

  }
}
