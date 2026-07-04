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

    private:
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
