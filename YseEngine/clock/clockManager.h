/*
  ==============================================================================

    clockManager.h
    Created for issue #249 — domain clocks.

  ==============================================================================
*/

#ifndef CLOCKMANAGER_H_INCLUDED
#define CLOCKMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include <string>

#include "domainClock.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace CLOCK {

    // Owns every domain clock and drives their beat/tempo advancement.
    //
    // Lifecycle follows the lock-free main->audio handoff the PLAYER / MIDI-file
    // managers use (#156 / #190): the control thread emplaces a clock into the
    // canonical list under `implementationsMutex` and hands it to the audio
    // thread through a lock-free inbox; the audio thread owns the `inUse`
    // working list, advances each clock every callback, and — when a clock is
    // released — retires it for the slow-pool delete job. The audio thread never
    // allocates, locks, or frees.
    //
    // Domain clocks are addressed by name, not by an interface handle, so the
    // control-thread queries (createClock / destroyClock / setTempo /
    // beatPosition / currentTempo) look the clock up by name under the mutex.
    // Those readers run on the control/UI thread (beatPosition at frame rate for
    // playhead display) and only touch atomics, so they never contend with the
    // audio callback.
    class managerObject {
    public:
      using ImplementationType = domainClock;

      managerObject();
      ~managerObject() noexcept;

      /** Create a clock named `name` starting at `initialTempo` BPM. Returns
          false if the name is empty or a live clock already owns it (first
          registration wins). Control thread only. */
      bool createClock(const std::string& name, Flt initialTempo);

      /** Flag the named clock for destruction. It stops being visible to queries
          immediately; the audio thread retires it and the slow pool reaps it.
          A no-op for an unknown name. Control thread only. */
      void destroyClock(const std::string& name);

      /** Whether a live clock with `name` exists. Control thread only. */
      bool clockExists(const std::string& name);

      /** Ramp the named clock toward `bpm` over `rampSeconds` (0 = instant).
          A no-op for an unknown name. Control thread only. */
      void setTempo(const std::string& name, Flt bpm, Flt rampSeconds);

      /** Current beat position (running integral of tempo) of the named clock,
          or 0 for an unknown name. Control/UI thread. */
      Dbl beatPosition(const std::string& name);

      /** Current tempo in BPM of the named clock, or 0 for an unknown name.
          Control/UI thread. */
      Flt currentTempo(const std::string& name);

      /** Audio-thread tick, driven every callback with the block duration in
          seconds. Drains newly-created clocks, advances each live clock, and
          retires released clocks for slow-pool deletion. */
      void update(Flt delta);

      /** Session teardown from INTERNAL::global::close(): join any in-flight
          delete job and clear every clock so no state persists across an
          init/close cycle. Called after the audio device is closed and both
          thread pools are joined. */
      void clear();

    private:
      // Find a live (non-released) clock by name. Caller holds implementationsMutex.
      domainClock* findLive(const std::string& name);

      // Canonical owner of every clock. Mutated only by the control thread
      // (createClock) and the slow-pool deleteJob (remove_if); guarded by
      // implementationsMutex. The audio thread never iterates it.
      std::forward_list<domainClock> implementations;
      std::mutex implementationsMutex;

      // Lock-free SPSC handoff: the control thread pushes a newly-created clock
      // here; the audio thread drains it into `inUse` at the top of update().
      lfQueue<domainClock*> toLoadInbox;

      // Audio-thread-owned working list. update() iterates and erases from this
      // list only; the clocks it points at live in `implementations`.
      std::forward_list<domainClock*> inUse;

      // Reaps clocks flagged OBJECT_DELETE off the audio thread on the slow pool.
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // Set by the audio thread when it retires a released clock from `inUse`;
      // drives the deleteJob enqueue on the following tick.
      aBool runDelete;

      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace CLOCK
} // namespace YSE

#endif // CLOCKMANAGER_H_INCLUDED
