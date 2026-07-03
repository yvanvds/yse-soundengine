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
      // ``owner`` is the pObject this outlet belongs to; it is the route from a
      // send back to the owning patcher's pinned GraphState (issue #226).
      outlet(pObject* owner, OUT_TYPE type);
      ~outlet();

      inline OUT_TYPE Type() const {
        return type;
      }

      void SendBang(THREAD thread);
      void SendFloat(float value, THREAD thread);
      void SendInt(int value, THREAD thread);
      void SendList(const std::string& value, THREAD thread);
      void SendMessage(const std::string& value, THREAD thread);
      void SendBuffer(DSP::buffer* value, THREAD thread);

      void Connect(inlet* in);
      void Disconnect(inlet* in);

      // Drop every connection and remove this outlet from the inlets it fed.
      // Used when an object is deleted so the next GraphState holds no
      // reference to it (issue #226). Mirrors the destructor's peer cleanup
      // but leaves the outlet allocated.
      void UnwireFromPeers();

      // Dense, lifetime-stable id assigned when the owning object is added to a
      // patcher; indexes GraphState::outletTargets. -1 until assigned (e.g. a
      // standalone object outside any patcher).
      inline int GraphId() const {
        return graphId;
      }
      inline void SetGraphId(int id) {
        graphId = id;
      }
      // The live (control-thread) fan-out list, copied into a GraphState at
      // build time.
      inline const std::vector<inlet*>& Targets() const {
        return connections;
      }

      void DumpJSON(nlohmann::json::value_type& json);

      unsigned int GetConnections();
      unsigned int GetTarget(unsigned int connection);
      unsigned int GetTargetInlet(unsigned int connection);

      // Documentation surface — see inlet::SetDoc for usage notes.
      void SetDoc(const std::string& label, const std::string& doc, const std::string& range);
      const std::string& GetDocLabel() const {
        return docLabel;
      }
      const std::string& GetDocDescription() const {
        return docDescription;
      }
      const std::string& GetRange() const {
        return docRange;
      }

    private:
      // Pinned-snapshot adjacency when the patcher is mid-block, else the live
      // wiring. See the definition for the full contract (issue #226).
      const std::vector<inlet*>& resolveTargets() const;

      OUT_TYPE type;

      // Owning object; the hop to the patcher's pinned GraphState. Never null
      // for outlets created through the ADD_OUT_* macros.
      pObject* owner;
      // See GraphId(). -1 = unassigned (standalone object, no patcher).
      int graphId = -1;

      std::vector<inlet*> connections;

      std::string docLabel;
      std::string docDescription;
      std::string docRange;
    };

  } // namespace PATCHER
} // namespace YSE
