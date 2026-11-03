/*
  ==============================================================================

    domainClock.h
    Created for issue #249 — domain clocks.

  ==============================================================================
*/

#ifndef DOMAINCLOCK_H_INCLUDED
#define DOMAINCLOCK_H_INCLUDED

#include "../headers/types.hpp"
#include "../headers/enums.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace YSE {
  namespace CLOCK {

    /** @brief What became of a tempo write a *patch* asked for (issue #1254).
     *
     *  Host writes (``system::setTempo``) always land and answer nothing; only
     *  the patcher's writers (``.transport``, ``.setclock``, ``.tempo``) go
     *  through a path that can be refused, and they need to tell a refusal
     *  apart from a clock that is not there so they can warn about the one
     *  that is the host's business. */
    enum class TempoWrite {
      WRITTEN, ///< Queued for the clock's next block.
      NO_CLOCK, ///< No such clock (or a binding that has not resolved yet).
      HOST_OWNED, ///< The host owns this clock's tempo; nothing was written.
    };

    /** @brief One named musical (beat) clock, derived from the audio callback.
     *
     *  A domain clock is a *beat accumulator*: every audio block it advances by
     *  ``blockSeconds × tempo / 60`` at its current tempo. Beat position is the
     *  running integral of tempo — there is no absolute-time schedule anywhere,
     *  which is what lets polytemporal domains keep exact, deterministic
     *  relationships (they all derive from the single sample clock).
     *
     *  Tempo is a playable, rampable control: ``requestTempo(bpm, rampSeconds)``
     *  slews toward a new tempo linearly over ``rampSeconds`` (instant when the
     *  ramp is 0). Tempo is not clamped — 0 pauses the clock, a negative tempo
     *  runs it backwards — because the primitive stays dumb; the UI steers it.
     *
     *  Threading: ``update()`` runs on the audio thread and owns the
     *  authoritative accumulator state. ``requestTempo`` / ``release`` are called
     *  from the control thread and hand their intent over through atomics; the
     *  cross-thread readers (``beatPosition`` / ``currentTempo``) read published
     *  snapshots and never touch the audio-thread-owned members.
     *
     *  Ownership of the tempo (issue #1254): a clock is a shared lever by
     *  default — the host and any patch naming it may both set its tempo. The
     *  host can mark a clock *host-owned* (``setHostOwned``); patch writes then
     *  go through ``requestPatchTempo`` and are refused, while ``requestTempo``
     *  (the host's route) and every read keep working. The flag is one atomic,
     *  so the refusal is as wait-free as the write it replaces.
     *
     *  Lifetime: a clock is owned by ``shared_ptr`` and every binding taken
     *  through ``managerObject::lookup`` holds a share (issue #707), so
     *  ``destroyClock`` cannot pull a clock out from under a bound clip
     *  transport or patcher binding. A released clock is dropped from the
     *  manager's working list and therefore stops advancing, so the snapshots
     *  a straggling holder still reads simply freeze at their last value —
     *  which is the same answer such a holder gets for a clock at tempo 0.
     */
    class domainClock {
    public:
      domainClock(const std::string& name, Flt initialTempo);

      /** @brief Audio-thread tick. Advance ``delta`` seconds of beat + tempo.
       *
       *  Consumes a pending ``requestTempo`` first, ramps the tempo, integrates
       *  the beat position over the block (linear-tempo midpoint), and publishes
       *  the snapshots read by the control thread. Returns ``false`` once the
       *  clock has been released (``destroyClock``) so the manager can retire it
       *  from its working list — mirrors ``PLAYER::implementationObject::update``.
       */
      bool update(Flt delta);

      /** @brief Control-thread. Queue a new tempo target reached over
       *         ``rampSeconds`` (clamped to ``>= 0``; 0 is instant). */
      void requestTempo(Flt bpm, Flt rampSeconds);

      /** @brief Any thread; wait-free. The patcher's tempo write (issue #1254).
       *
       *  ``requestTempo`` unless the host owns this clock, in which case
       *  nothing is written and ``HOST_OWNED`` comes back. A patch write that
       *  passed the check just before the host set the flag still lands; the
       *  flag guards against patches, not against a race the host starts. */
      TempoWrite requestPatchTempo(Flt bpm, Flt rampSeconds) {
        if (isHostOwned()) return TempoWrite::HOST_OWNED;
        requestTempo(bpm, rampSeconds);
        return TempoWrite::WRITTEN;
      }

      /** @brief Control-thread. Mark the clock's tempo as the host's, or give
       *         it back to everyone (issue #1254). */
      void setHostOwned(bool on) {
        hostOwned.store(on, std::memory_order_release);
      }

      /** @brief Cross-thread read: whether patch tempo writes are refused. */
      bool isHostOwned() const {
        return hostOwned.load(std::memory_order_acquire);
      }

      /** @brief Control-thread. Flag the clock for retirement by the manager. */
      void release() {
        released.store(true, std::memory_order_release);
      }

      /** @brief Immutable domain name. Read on the control thread under the
       *         manager mutex. */
      const std::string& getName() const {
        return name;
      }

      /** @brief Cross-thread read: running beat position (integral of tempo). */
      Dbl beatPosition() const {
        return beatOut.load(std::memory_order_acquire);
      }

      /** @brief Cross-thread read: current tempo in BPM. */
      Flt currentTempo() const {
        return tempoOut.load(std::memory_order_acquire);
      }

      /** @brief Whether ``release`` has been called (control thread). */
      bool isReleased() const {
        return released.load(std::memory_order_acquire);
      }

      // ---- slow-pool delete handoff (mirrors PLAYER / the manager templates) --
      void setStatus(OBJECT_IMPLEMENTATION_STATE value) {
        objectStatus.store(value);
      }

      /** @brief Slow-pool delete-job predicate.
       *
       *  Takes the owning handle rather than the object because the manager
       *  owns its clocks by ``shared_ptr`` (issue #707). Dropping the handle
       *  here retires the *manager's* share; the clock itself is freed only
       *  once the last holder — a bound clip transport, a patcher clock
       *  binding — has let go of its own share. */
      static bool canBeDeleted(const std::shared_ptr<domainClock>& c) {
        return c->objectStatus.load() == OBJECT_DELETE;
      }

    private:
      std::string name; // immutable after construction

      // Authoritative state — only touched on the audio thread in update().
      Dbl beat;
      Dbl tempo;
      Dbl tempoTarget;
      Dbl tempoRate; // BPM per second, signed; 0 when not ramping
      bool ramping;

      // Published snapshots for cross-thread reads.
      std::atomic<Dbl> beatOut;
      aFlt tempoOut;

      // setTempo request handoff (control -> audio).
      aFlt reqTempo;
      aFlt reqRamp;
      aBool reqDirty;

      // Set by the host (issue #1254): patch tempo writes are refused while it
      // is true. Read by any thread, including the audio callback.
      aBool hostOwned;

      // Lifecycle. `released` is set by destroyClock on the control thread;
      // `objectStatus` reaches OBJECT_DELETE once the audio thread has retired
      // the clock from the manager's working list, at which point the slow-pool
      // delete job may reap it. The audio thread never frees a clock.
      aBool released;
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;
    };

  } // namespace CLOCK
} // namespace YSE

#endif // DOMAINCLOCK_H_INCLUDED
