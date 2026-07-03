#pragma once
#include "../headers/defines.hpp"
#include "pObject.h"
#include "pHandle.hpp"
#include "../dsp/buffer.hpp"
#include "patcher.hpp"
#include "graphState.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace YSE {
  namespace PATCHER {
    class pOutput;

    class patcherImplementation : public pObject {
    public:
      patcherImplementation(int mainOutputs, patcher* head);
      virtual ~patcherImplementation();

      // Patcher name used as the bus prefix for inner gSend / gReceive
      // routing (issue #122). Default is an auto-generated "patcher_<N>"
      // identifier (process-wide counter). SetName() re-subscribes every
      // gReceive in the patcher so that a rename is transparent.
      const std::string& Name() const {
        return patcherName;
      }
      void SetName(const std::string& n);

      virtual const char* Type() const;
      virtual void ResetDSP();
      virtual void Calculate(THREAD thread);

      // The GraphState pinned for the block currently being rendered, or null
      // between blocks. Read by inlets/outlets to resolve topology without a
      // lock (issue #226).
      const GraphState* CurrentBlockGraph() const {
        return currentBlockGraph_.load(std::memory_order_acquire);
      }

      virtual void SetMessage(const std::string&, float) {}

      pHandle* CreateObject(const std::string& type, const std::string& args);
      void DeleteObject(pHandle* obj);
      void Clear();

      void Connect(pHandle* from, int outlet, pHandle* to, int inlet);
      void Disconnect(pHandle* from, int outlet, pHandle* to, int inlet);

      std::string DumpJSON();
      void ParseJSON(const std::string& content);

      unsigned int Objects();
      YSE::pHandle* GetHandleFromList(unsigned int obj);
      YSE::pHandle* GetHandleFromID(unsigned int objID);

      std::vector<YSE::DSP::buffer> output;

      aBool controlledBySound;
      std::atomic<patcher*> head;

      // for external data input
      bool PassBang(const std::string& to, THREAD thread);
      bool PassData(int value, const std::string& to, THREAD thread);
      bool PassData(float value, const std::string& to, THREAD thread);
      bool PassData(const std::string& value, const std::string& to, THREAD thread);

      void SetHandler(oscHandler* handler);

    private:
      // Compile the current object wiring into a fresh immutable GraphState.
      // Control-thread only (allocates). See graphState.h.
      GraphState* BuildGraph();
      // Stamp lifetime-stable graph ids on a newly added object's inlets/outlets.
      void AssignGraphIds(pObject* obj);
      // Build the next GraphState, publish it with one atomic swap, retire the
      // previous one, and reclaim anything the audio thread has moved past.
      void RebuildAndPublish();
      // Free retired GraphStates/objects the audio thread can no longer be
      // using (interim reclamation; the real epoch scheme is issue #227).
      void DrainRetired();
      // Unconditionally free everything retired plus the active graph. Called
      // from the destructor, where the audio thread is already stopped.
      void FreeAllRetired();

      std::mutex mtx;
      bool fileHandlerActive;
      std::map<pHandle*, pObject*> objects;
      oscHandler* oscHandle = nullptr;

      // Published topology snapshot the audio thread reads (issue #226). Only
      // the control thread writes it, under mtx.
      std::atomic<const GraphState*> active_{nullptr};
      // Snapshot pinned for the duration of the block being rendered. Written
      // by the audio thread at the start/end of Calculate.
      std::atomic<const GraphState*> currentBlockGraph_{nullptr};
      // Monotonic count of rendered blocks, used by the interim reclaimer to
      // tell when the audio thread has advanced past a retired snapshot.
      std::atomic<std::uint64_t> audioBlock_{0};
      // Retired snapshots / deleted objects awaiting reclamation, tagged with
      // the block count at retirement. Control-thread only, guarded by mtx.
      std::vector<std::pair<const GraphState*, std::uint64_t>> retiredGraphs_;
      std::vector<std::pair<pObject*, std::uint64_t>> retiredObjects_;
      // Per-patcher dense id counters for inlets / outlets. Never reused, so
      // ids stay stable across edits.
      int nextInletId_ = 0;
      int nextOutletId_ = 0;

      // Bus-prefix for inner gSend/gReceive routing (issue #122). Defaulted
      // to "patcher_<N>" in the ctor; mutated by SetName().
      std::string patcherName;

      std::string GetRecieveObjectsAsString();
    };

  } // namespace PATCHER
} // namespace YSE
