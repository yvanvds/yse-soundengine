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
