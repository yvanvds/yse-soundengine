/*
  ==============================================================================

    midifileManager.h
    Created: 13 Jul 2014 5:21:20pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILEMANAGER_H_INCLUDED
#define MIDIFILEMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include "midifileImplementation.h"
#include "midifile.hpp"
#include "../internal/threadPool.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace MIDI {

    class managerObject {
    public:
      using ImplementationType = fileImpl;

      managerObject();
      ~managerObject() noexcept;

      fileImpl* addImplementation(file* head);
      void update();

    private:
      // Canonical list of all fileImpls. Touched by the main thread
      // (addImplementation emplace_front) and by the slow-pool deleteJob
      // (remove_if). Guarded by implementationsMutex — never iterated on the
      // audio thread (issue #190).
      std::forward_list<fileImpl> implementations;

      // Guards `implementations` between main thread and slow-pool worker.
      std::mutex implementationsMutex;

      // Lock-free SPSC inbox: the main thread pushes a new impl here from
      // addImplementation(); the audio thread drains it into `inUse` at the top
      // of update(). MIDI files need no async setup, so impls go straight into
      // use — there is no intermediate `toLoad` stage.
      lfQueue<fileImpl*> toLoadInbox;

      // Audio-thread-owned working list. update() iterates and erases from this
      // list only; the impls it points at live in `implementations`.
      std::forward_list<fileImpl*> inUse;

      // Reaps impls flagged OBJECT_DELETE off the audio thread on the slow pool.
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Set by the audio thread when it retires an orphaned impl from `inUse`;
      // drives the deleteJob enqueue on the following tick.
      aBool runDelete;

      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();
  } // namespace MIDI
} // namespace YSE

#endif // MIDIFILEMANAGER_H_INCLUDED
