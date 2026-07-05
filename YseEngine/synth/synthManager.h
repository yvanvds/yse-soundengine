/*
  ==============================================================================

    synthManager.h
    Singleton manager for the YSE::synth subsystem.

    Mirrors the sound manager's modern, job-based idiom (managerSetupJob /
    managerDeleteJob + the OBJECT_* state machine) — voice cloning happens on
    the setup pool, deletion on the delete pool, and the audio-thread tick only
    drives lifecycle. Implements §8 of docs/design/synth_core.md.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SYNTHMANAGER_H
#define YSE_SYNTH_SYNTHMANAGER_H

#include <forward_list>
#include <mutex>

#include "../internal/managerJobs.hpp"
#include "../internal/threadPool.h"
#include "../utils/intrusiveForwardList.hpp"
#include "../utils/lfQueue.hpp"
#include "synth.hpp"
#include "synthImplementation.h"
#include "synthInterface.hpp"
#include "synthMessage.h"

namespace YSE {
  namespace SYNTH {

    /**
      Owns every synth implementationObject and drives their lifecycle. There is
      one process-wide instance, reached through Manager().
    */
    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      /** Create and register a new implementation for `head`. */
      implementationObject* addImplementation(interfaceObject* head);

      /** Move `impl` (freshly configured) into the setup pipeline: mark it
          OBJECT_CREATED and hand it to the audio thread via the lock-free
          inbox, which schedules the setup-pool clone job. */
      void setup(implementationObject* impl);

      /** Audio-thread lifecycle tick — called once per callback. Never renders
          audio (that happens inside each impl's outputSource); this only drains
          the inbox, schedules setup/delete jobs, promotes ready impls, and
          releases teardown impls. */
      void update();

      /** Audio-thread-only "nothing alive" signal. */
      Bool empty();

    private:
      void drainInbox();
      void promoteReadyImpls();
      void syncAndReleaseInUse();

      INTERNAL::managerSetupJob<managerObject> mgrSetup;
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Audio-thread-owned lists (intrusive, allocation-free — issue #194).
      IntrusiveForwardList<implementationObject, &implementationObject::_mgrNext> inUse;
      IntrusiveForwardList<implementationObject, &implementationObject::_mgrNext> toLoad;

      // Lock-free main->audio handoff of newly registered impls.
      lfQueue<implementationObject*> toLoadInbox;

      // Canonical owner of every impl. Touched by the main thread
      // (addImplementation) and the delete pool (deleteJob); guarded by
      // implementationsMutex. The audio thread never iterates it.
      std::forward_list<implementationObject> implementations;
      std::mutex implementationsMutex;

      aBool runDelete;

      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SYNTHMANAGER_H
