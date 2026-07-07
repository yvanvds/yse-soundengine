/*
  ==============================================================================

    channelManager.h
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELMANAGER_H_INCLUDED
#define CHANNELMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "channelImplementation.h"
#include "../headers/enums.hpp"
#include "../classes.hpp"
#include "../internalHeaders.h"
#include "../internal/threadPool.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"
#include "../utils/intrusiveForwardList.hpp"

namespace YSE {
  namespace CHANNEL {

    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      void update();

      /** Tear down all per-session channel state so the manager can be
          re-created by a subsequent System::init(). Called from system::close()
          after both thread pools are joined and the audio device is closed,
          with Global().active already false. Clearing `implementations` runs
          each impl's destructor, which nulls its interface's pimpl — including
          the persistent master/ambient/fx/music/gui/voice channels — so
          channel::create()/createGlobal() do not trip their
          assert(pimpl == nullptr) on the next init (issue #132).
      */
      void destroy();

      implementationObject* addImplementation(channel* head);
      void setup(implementationObject* impl);
      Bool empty();

      // channel output configuration from interface
      void setChannelConf(CHANNEL_TYPE type, Int outputs = 2);

      // switch to the new configureation during audio callback
      void changeChannelConf();

      UInt getNumberOfOutputs();
      Flt getOutputAngle(UInt nr);

      /** @brief True when output @p nr is the low-frequency-effects (.1) channel.
          The LFE output is excluded from azimuth panning; positional sounds are
          never panned into it. Returns false when @p nr is out of range or the
          current layout has no LFE. */
      Bool getOutputIsLFE(UInt nr);

      channel& master();
      channel& FX();
      channel& music();
      channel& ambient();
      channel& voice();
      channel& gui();

      void setMaster(implementationObject* impl);

      /////////////////////////////////////////////////////
      // Send / return buses (issue #165)
      /////////////////////////////////////////////////////
      // Audio-thread render helpers, driven from the master's buffersToParent().
      /** Zero every return's `out` buffer once per block, before any send taps. */
      void zeroReturnBuffers();
      /** Generation-ordered returns phase: each return's insert chain runs on the
          fast pool, then folds into @p master. Audio thread only. */
      void processReturns(implementationObject* master);
      /** Link/unlink a return into the audio-thread `returns` render list. Called
          from the impl's doThisWhenReady() / detachSends(). */
      void linkReturn(implementationObject* r);
      void unlinkReturn(implementationObject* r);

      // Control-thread wiring-graph operations (issue #165 §10). These run on the
      // setup/control thread only (from the channel interface), never on the
      // audio thread. They validate acyclicity, maintain the return→return edge
      // graph, and post SET_GENERATION messages when a wiring change alters the
      // longest-path generation of any return. Guarded by returnGraphMutex.
      /** Register/unregister a return as a node in the wiring graph. */
      void registerReturnGraph(implementationObject* r);
      void unregisterReturnGraph(implementationObject* r);
      /** Add a return→return edge iff it keeps the graph acyclic. Returns false
          (edge NOT added) if it would create a cycle. Recomputes generations. */
      bool tryAddReturnEdge(implementationObject* from, implementationObject* to);
      /** Remove one return→return edge (by multiplicity). Recomputes generations. */
      void removeReturnEdge(implementationObject* from, implementationObject* to);
      /** The generation the wiring graph currently assigns to return @p r, or -1
          if it is not a registered return. Control-thread diagnostic (tests). */
      Int returnGenerationOf(implementationObject* r);

    private:
      // Recompute the longest-path generation of every return over the current
      // return→return DAG and post SET_GENERATION to any whose value changed.
      // Control thread, called under returnGraphMutex.
      void recomputeAndPostGenerations();
      // True if `to` can already reach `from` over return→return edges (so adding
      // from→to would close a cycle). Control thread, called under the mutex.
      bool returnReachable(implementationObject* from, implementationObject* to);

      // Once an object is ready for use, it is linked into this container. The
      // manager updates and syncs all these objects during the dsp callback.
      // Intrusive (link embedded via `_mgrNext`) — allocation-free (issue #194).
      IntrusiveForwardList<implementationObject, &implementationObject::_mgrNext> inUse;

      INTERNAL::managerSetupJob<managerObject> mgrSetup;
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Lock-free SPSC inbox: main thread pushes here from setup(); audio
      // thread drains it into `toLoad` at the top of update().
      lfQueue<implementationObject*> toLoadInbox;

      // Audio-thread-owned working list of impls awaiting OBJECT_READY. Shares
      // the `_mgrNext` link with `inUse` (an impl is only ever in one of them).
      IntrusiveForwardList<implementationObject, &implementationObject::_mgrNext> toLoad;

      // Audio-thread-owned list of return buses (issue #165). Threaded on the
      // impl's `_returnNext` link — distinct from `_mgrNext` because a return is
      // in BOTH `inUse` (lifecycle/sync) and `returns` (render phase). Walked
      // only in zeroReturnBuffers()/processReturns() and mutated only via
      // linkReturn()/unlinkReturn(), all on the audio callback thread.
      IntrusiveForwardList<implementationObject, &implementationObject::_returnNext> returns;

      // ─── Control-thread return→return wiring graph (issue #165 §10) ───
      // Touched ONLY on the setup/control thread, never on the audio thread, so
      // its mutex never reaches the render path. `returnEdges[from][to]` is the
      // multiplicity of send slots wiring return `from` into return `to`; an edge
      // is present while the count is > 0. `returnGenPosted` caches the last
      // generation posted to each return so recomputes only message the ones that
      // changed.
      std::mutex returnGraphMutex;
      std::unordered_set<implementationObject*> returnNodes;
      std::unordered_map<implementationObject*, std::unordered_map<implementationObject*, int>>
          returnEdges;
      std::unordered_map<implementationObject*, Int> returnGenPosted;

      // Canonical list of all implementationObjects. Touched by main thread
      // (addImplementation emplace_front) and the slow-pool worker (setupJob
      // iterates, deleteJob remove_ifs). Guarded by implementationsMutex.
      std::forward_list<implementationObject> implementations;

      // Guards `implementations` between main thread and slow-pool worker.
      std::mutex implementationsMutex;

      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      channel _master;
      channel _fx;
      channel _music;
      channel _ambient;
      channel _voice;
      channel _gui;

      // channel output configuration. `outputAngles` holds the azimuth (radians)
      // of each physical output; `outputIsLFE` marks the low-frequency-effects
      // (.1) output so it is kept out of azimuth panning. Both arrays are sized
      // to `outputChannels` and value-initialised in changeChannelConf().
      aFlt* outputAngles;
      aBool* outputIsLFE;
      aUInt outputChannels;
      std::atomic<CHANNEL_TYPE> channelType;

      // Bounds-checked writers used by the layout presets below, so a preset can
      // never write past the allocated `outputChannels` even if it is applied to
      // a device with fewer channels than the layout expects.
      void setAngle(UInt idx, Flt degrees);
      void setLFE(UInt idx);

      void setMono();
      void setStereo();
      void setQuad();
      void set51();
      void set51Side();
      void set61();
      void set71();
      void setAuto(Int count);

      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace CHANNEL
} // namespace YSE

#endif // CHANNELMANAGER_H_INCLUDED
