#pragma once
#include "../headers/defines.hpp"
#include "pObject.h"
#include "pHandle.hpp"
#include "../dsp/buffer.hpp"
#include "patcher.hpp"
#include "graphState.h"
#include "../utils/mpmcQueue.hpp"
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

      // for external data input. These run on the control/GUI thread: they no
      // longer poke inlets directly (that raced the audio thread's Calculate and
      // was a genuine UAF through Parameters::Set — issue #225). Instead a value
      // message is enqueued on a lock-free queue and delivered to the inlet on
      // the audio thread at the top of the next Calculate. The bool return still
      // means "a matching gReceive exists in this patcher (message enqueued)";
      // when none exists the call falls back to the OSC handler as before.
      bool PassBang(const std::string& to, THREAD thread);
      bool PassData(int value, const std::string& to, THREAD thread);
      bool PassData(float value, const std::string& to, THREAD thread);
      bool PassData(const std::string& value, const std::string& to, THREAD thread);

      void SetHandler(oscHandler* handler);

    private:
      // Kinds of deferred value message carried on the SPSC command queue
      // (issue #225). One per PassBang / PassData overload.
      enum class ValueKind : std::uint8_t { Bang, Int, Float, List };

      // Longest target (gReceive) name and list payload that ride the queue
      // inline. Names/values longer than this can't be delivered through the
      // RT-safe path; the control thread logs and drops rather than truncating
      // silently or allocating. Chosen well above any realistic identifier /
      // space-separated value list.
      static constexpr std::size_t kValueNameCap = 64;
      static constexpr std::size_t kValueListCap = 256;
      // Fixed queue depth (messages buffered between two audio blocks). Bounded
      // and allocated once; a full queue drops-with-log, never blocks the
      // control thread and never allocates on the audio thread.
      static constexpr std::size_t kValueQueueCapacity = 256;

      // A deferred value delivery. Trivially copyable (POD) so it can ride the
      // lock-free mpmcQueue by value — no heap, so nothing to reclaim. The
      // target receiver is carried by name and re-resolved against the pinned
      // GraphState on the audio thread, which makes delivery inherently safe
      // across a concurrent DeleteObject: a removed receiver is simply absent
      // from the snapshot and the message is dropped.
      struct ValueMsg {
        ValueKind kind;
        int intVal;
        float floatVal;
        char target[kValueNameCap]; // null-terminated gReceive DataName()
        char listVal[kValueListCap]; // null-terminated; List kind only
      };

      // Deliver every queued value message against the pinned snapshot. Audio
      // thread only, called from Calculate after currentBlockGraph_ is stored so
      // value propagation resolves through the snapshot. Dispatches with T_GUI
      // semantics (set the parameter; the block's own traversal renders it).
      void DeliverPendingValues(const GraphState* g);

      // Find the gReceive named `to` in snapshot g and deliver the value to its
      // inlet 0. Audio thread only (no allocation: `to` is a C-string and the
      // list value is a caller-owned reference). Returns true iff a matching
      // receiver exists. Shared by the deferred drain and the synchronous
      // audio-thread path (gSend fan-out during traversal).
      bool DispatchToReceiver(const GraphState* g, ValueKind kind, const char* to, int intVal,
                              float floatVal, const std::string& listVal, THREAD thread);

      // Control thread. Copies `to` into msg.target, checks whether a matching
      // gReceive exists in this patcher, and if so enqueues msg. Returns true
      // iff a matching receiver exists (so the caller only falls back to OSC
      // when there is genuinely no in-patcher target).
      bool EnqueueValue(ValueMsg& msg, const std::string& to);

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

      // Lock-free command queue for deferred value delivery (issue #225).
      // Producers: control/GUI threads (PassBang / PassData). Consumer: the
      // audio thread, draining in Calculate. mpmcQueue is a superset of SPSC, so
      // concurrent producers are safe too.
      YSE::mpmcQueue<ValueMsg> valueQueue_{kValueQueueCapacity};
      // Audio-thread-only scratch buffer for delivering List payloads. Reserved
      // to kValueListCap in the ctor so assigning any inline list value reuses
      // this buffer instead of allocating on the audio thread, while still
      // handing inlet::SetList the const std::string& it expects.
      std::string listScratch_;

      // Bus-prefix for inner gSend/gReceive routing (issue #122). Defaulted
      // to "patcher_<N>" in the ctor; mutated by SetName().
      std::string patcherName;

      std::string GetRecieveObjectsAsString();
    };

  } // namespace PATCHER
} // namespace YSE
