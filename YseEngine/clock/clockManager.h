/*
  ==============================================================================

    clockManager.h
    Created for issue #249 — domain clocks.

  ==============================================================================
*/

#ifndef CLOCKMANAGER_H_INCLUDED
#define CLOCKMANAGER_H_INCLUDED

#include <forward_list>
#include <memory>
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
    //
    // Ownership is *shared*, not exclusive (issue #707). A clock bound through
    // `lookup` — by a clip transport or by a patcher clock binding — is read
    // from the audio callback through a raw pointer, and neither holder can be
    // told when it is safe to stop: the patcher's bridge never releases a
    // binding by design, and a transport cannot observe the audio thread
    // letting go of a pointer it published. So `destroyClock` retires a clock
    // rather than deleting it — it disappears from queries at once, the audio
    // thread stops advancing it, and the reap drops the manager's share; the
    // object itself outlives the manager's interest in it for exactly as long
    // as some binding still holds a share.
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
          immediately, the audio thread stops advancing it, and the slow pool
          drops the manager's share of it. Anything still bound to it through
          ``lookup`` keeps it alive (see there). A no-op for an unknown name.
          Control thread only. */
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

      /** Resolve a live clock by name to a stable handle a clip transport or a
          patcher clock binding can hold and read (``beatPosition``) every audio
          block. Returns an empty handle for an unknown name. Control thread or
          background pool (anywhere the manager mutex may be taken).

          The handle is a *share of the clock's lifetime*, not a borrowed
          pointer (issue #707): holding one guarantees the clock stays alive for
          as long as the holder does, whatever ``destroyClock`` and the reap do
          in the meantime. This is deliberately not the caller-managed contract
          the engine's other cross-object bindings use, because neither holder
          can keep it: ``PATCHER::clockBridge`` never releases a binding by
          design, and a clip transport cannot tell when the audio thread has
          stopped reading the pointer it published. A destroyed clock stops
          advancing, so a straggling holder reads a frozen beat rather than
          freed memory.

          Raw ``domainClock*`` reads off the handle stay a plain load of a
          published atomic, so the audio thread may poll one every block. What
          the audio thread must never do is copy or drop the handle itself —
          that is a refcount operation. */
      std::shared_ptr<domainClock> lookup(const std::string& name);

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
      // Find a live (non-released) clock by name, as a share of its lifetime.
      // Empty when there is none. Caller holds implementationsMutex.
      std::shared_ptr<domainClock> findLive(const std::string& name);

      // The manager's share of every clock. Mutated only by the control thread
      // (createClock) and the slow-pool deleteJob (remove_if); guarded by
      // implementationsMutex. The audio thread never iterates it.
      //
      // Shared rather than direct ownership (issue #707): the reap drops the
      // manager's handle, but a clock a clip transport or a patcher binding is
      // still holding stays alive until that holder is gone.
      std::forward_list<std::shared_ptr<domainClock>> implementations;
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
