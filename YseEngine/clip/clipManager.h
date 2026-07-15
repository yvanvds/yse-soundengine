/*
  ==============================================================================

    clipManager.h
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#ifndef YSE_CLIP_CLIPMANAGER_H
#define YSE_CLIP_CLIPMANAGER_H

#include <forward_list>
#include <mutex>

#include "clipTransport.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace CLIP {

    // Owns every clip transport and advances them from the audio callback.
    //
    // Lifecycle follows the lock-free main->audio handoff the CLOCK / MIDI-file
    // managers use (#249 / #190): the control thread emplaces a transport into
    // the canonical list under `implementationsMutex` and hands it to the audio
    // thread through a lock-free inbox; the audio thread owns the `inUse`
    // working list, advances each transport every callback, and — once the
    // interface is destroyed — retires it for the slow-pool delete job. The
    // audio thread never allocates, locks, or frees.
    class managerObject {
    public:
      using ImplementationType = transport;

      managerObject();
      ~managerObject() noexcept;

      /** Create a transport bound to a public `clip` interface. Control thread. */
      transport* addImplementation(clip* head);

      /** Audio-thread tick, driven every callback after the clocks advance:
          drains newly-created transports, advances each live transport (which
          reads its clock's fresh beat window), and retires orphaned ones. */
      void update();

      /** Session teardown from INTERNAL::global::close(): join any in-flight
          delete job and clear every transport. Called after the audio device is
          closed and both thread pools are joined. */
      void clear();

    private:
      // Canonical owner of every transport. Mutated only by the control thread
      // (addImplementation) and the slow-pool deleteJob (remove_if); guarded by
      // implementationsMutex. The audio thread never iterates it.
      std::forward_list<transport> implementations;
      std::mutex implementationsMutex;

      // Lock-free SPSC handoff: control thread pushes a new transport here; the
      // audio thread drains it into `inUse` at the top of update().
      lfQueue<transport*> toLoadInbox;

      // Audio-thread-owned working list.
      std::forward_list<transport*> inUse;

      // Reaps transports flagged OBJECT_DELETE off the audio thread on the slow
      // pool.
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Set by the audio thread when it retires an orphan from `inUse`; drives
      // the deleteJob enqueue on the following tick.
      aBool runDelete;

      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace CLIP
} // namespace YSE

#endif // YSE_CLIP_CLIPMANAGER_H
