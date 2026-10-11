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
    struct GraphState;
    class graphReadScope;

    struct outlet {
      // ``owner`` is the pObject this outlet belongs to; it is the route from a
      // send back to the owning patcher's pinned GraphState (issue #226).
      outlet(pObject* owner, OUT_TYPE type);
      ~outlet();

      inline OUT_TYPE Type() const {
        return type;
      }

      // A link outlet (issue #1038, ADD_OUT_LINK): a BUFFER outlet whose cord
      // shares a resource by reference instead of carrying samples, as
      // ~tapin's shares its delay line with ~tapout. It connects only to an
      // inlet that accepts links (inlet::RegisterLink), it is never pushed,
      // and the cord is not a sample dependency, so a loop through it is legal.
      // Its owner never calls SendBuffer on it.
      inline bool IsLink() const {
        return link;
      }
      inline void MarkLink() {
        link = true;
      }
      inline pObject* Owner() const {
        return owner;
      }

      void SendBang(THREAD thread);
      void SendFloat(float value, THREAD thread);
      void SendInt(int value, THREAD thread);
      void SendList(const std::string& value, THREAD thread);
      void SendMessage(const std::string& value, THREAD thread);
      void SendBuffer(DSP::buffer* value, THREAD thread);

      // ─── Armed control output (issue #1037) ─────────────────────────────
      //
      // How a signal object sends a control message. CALC() must not call
      // Send* on a control outlet: that runs the downstream handlers mid
      // traversal, while other signal objects are still waiting on their
      // inputs. It arms the outlet instead, and the patcher sends what was
      // armed once the block's traversal has finished (see
      // pObject::FlushArmedOutlets for when, in what order and on which
      // thread).
      //
      // One slot per outlet, holding one message. Arming again in the same
      // block replaces it: the latest float or int wins, and any number of
      // bangs is one bang. A message carries no sample offset; an object that
      // needs sample accuracy (an interval, a count) computes it itself and
      // sends the result.
      //
      // Audio thread only, and only the thread rendering the owning patcher:
      // CALC() writes the slot and the flush, later in the same block, reads
      // it. Plain fields, no atomics, no allocation. Ignored on a BUFFER
      // outlet.
      void ArmBang();
      void ArmFloat(float value);
      void ArmInt(int value);
      inline bool Armed() const {
        return armed != armedKind::NONE;
      }
      // Send what is armed, if anything, and disarm. Called by
      // pObject::FlushArmedOutlets.
      void FlushArmed(THREAD thread);

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
      // The fan-out in the scope's snapshot, or the live wiring where nothing
      // else writes it. See the definition for the full contract (issues
      // #226, #963).
      const std::vector<inlet*>& resolveTargets(const graphReadScope& scope) const;
      // Whether `graph` records this outlet as the owner of its graph id.
      bool OwnedBy(const GraphState* graph) const;

      OUT_TYPE type;
      // See IsLink().
      bool link = false;

      // The armed slot (issue #1037). See ArmBang above.
      enum class armedKind : unsigned char { NONE, BANG, FLOAT, INT };
      armedKind armed = armedKind::NONE;
      float armedFloat = 0.f;
      int armedInt = 0;

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
