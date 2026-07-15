/*
  ==============================================================================

    playerManager.h
    Created: 9 Apr 2015 1:39:02pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYERMANAGER_H_INCLUDED
#define PLAYERMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include "player.hpp"
#include "playerImplementation.h"
#include "../synth/synth.hpp" // SYNTH::interfaceObject — the synth a player drives (#156)
#include "../internal/managerJobs.hpp"
#include "../internal/threadPool.h"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace PLAYER {

    // Owns every player implementationObject and drives generation + lifecycle.
    // Reworked for #156 onto the lock-free main->audio handoff + slow-pool delete
    // pattern the MIDI-file manager uses (#155): the main thread emplaces under a
    // mutex and hands the impl to the audio thread via a lock-free inbox; the
    // audio thread owns the working list and never frees an impl (issue #190).
    // This replaces the previous single forward_list that emplaced on the main
    // thread while the audio thread iterated and erased it — a data race plus an
    // audio-thread free, both dormant only because the player had no create()
    // path (issue #268).
    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      /** Register a new player bound to `instrument` (the synth it feeds). Main
          thread only: emplaces into the canonical list under the mutex and hands
          the impl to the audio thread through the lock-free inbox. */
      implementationObject* addImplementation(player* head, SYNTH::interfaceObject* instrument);

      /** Audio-thread tick, driven every callback. Drains newly-created impls
          into the working list, advances each live player's note generation, and
          retires orphaned impls for slow-pool deletion. `delta` is the block
          duration in seconds. */
      void update(Flt delta);

    private:
      // Canonical owner of every impl. Touched by the main thread
      // (addImplementation) and the slow-pool deleteJob (remove_if); guarded by
      // implementationsMutex. The audio thread never iterates it.
      std::forward_list<implementationObject> implementations;
      std::mutex implementationsMutex;

      // Lock-free SPSC handoff: the main thread pushes a newly-created impl here;
      // the audio thread drains it into `inUse` at the top of update(). Players
      // need no async setup, so impls go straight into use.
      lfQueue<implementationObject*> toLoadInbox;

      // Audio-thread-owned working list. update() iterates and erases from this
      // list only; the impls it points at live in `implementations`.
      std::forward_list<implementationObject*> inUse;

      // Reaps impls flagged OBJECT_DELETE off the audio thread on the slow pool.
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Set by the audio thread when it retires an orphaned impl from `inUse`;
      // drives the deleteJob enqueue on the following tick.
      aBool runDelete;

      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace PLAYER
} // namespace YSE

#endif // PLAYERMANAGER_H_INCLUDED
