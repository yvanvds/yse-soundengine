/*
  ==============================================================================

    phasor.hpp
    A 0..1 phase ramp (issue #1045).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASOR_HPP
#define YSE_DSP_GENERATORS_PHASOR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "beatSpan.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A phase ramp: a sawtooth rising from 0 towards 1 at a given
     *         frequency, then wrapping back to 0.
     *
     *  The phase source behind wavetable reads, sequencers and anything else
     *  that needs to know "where in the cycle" it is. The output is the phase
     *  itself, not a bipolar audio saw, and it is not band-limited: it jumps
     *  from just under 1 to 0 once per cycle. For an antialiased sawtooth use
     *  ``blSaw``.
     *
     *  A negative frequency runs the ramp backwards, from 1 down to 0. The
     *  phase is kept in double precision, so a slow ramp does not drift over
     *  long runs.
     *
     *  **Following a beat clock** (issue #1278). Given a ``beatSpan`` and a
     *  period in beats, the ramp takes its speed from the clock instead of a
     *  frequency: one cycle every ``periodBeats`` beats, faster when the
     *  clock speeds up, still while it is stopped, backwards while it runs
     *  backwards. Unlocked, the ramp keeps its own phase and only its speed
     *  follows. Locked, the phase *is* the clock's position: beat ``b`` is
     *  phase ``b / periodBeats + offset`` (wrapped), so a locked ramp with a
     *  period of 4 starts its cycle on every fourth beat of the clock, and
     *  every locked ramp on the same clock agrees. While the span has no
     *  reading the ramp holds.
     *
     *  **Sync update.** With ``syncUpdate(true)`` a new frequency (or period
     *  in beats, unlocked) takes effect only when the ramp wraps, so a
     *  wavetable read from it changes pitch at the start of a cycle rather
     *  than in the middle of one; ``update()`` takes it at once. A frequency
     *  signal and a locked ramp are not held: the one changes every sample,
     *  and the other has no cycle of its own to wait for.
     *
     *  The sample rate is read from ``SAMPLERATE`` on every block, so the
     *  ramp keeps its frequency across an engine restart at another rate.
     *
     *  Apart from the constructor and the phase accessors, call it only from
     *  the audio thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API phasor {
    public:
      phasor();

      /** @brief One block at a constant ``frequency`` in Hz. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency in Hz. */
      buffer& operator()(buffer& frequency);

      /**
       *  @brief One block following a beat clock: one cycle every
       *         @p periodBeats beats of @p clock (issue #1278).
       *
       *  Unlocked, the ramp moves ``clock.step() / periodBeats`` a sample from
       *  its own phase. With @p lock the phase is the clock's beat over
       *  @p periodBeats plus @p offset, wrapped. The block is
       *  ``clock.length()`` samples. A period that is not above 0, or a span
       *  without a reading, holds the ramp where it is.
       */
      buffer& operator()(const beatSpan& clock, Dbl periodBeats, bool lock, Flt offset = 0.f);

      /** @brief Hold a new frequency or period until the ramp wraps. Off by
       *         default. */
      phasor& syncUpdate(bool on);
      bool syncUpdate() const {
        return syncOn;
      }

      /** @brief Take the frequency or period of the next block at once, even
       *         with ``syncUpdate`` on. */
      phasor& update();

      /**
       *  @brief Jump to a phase now. The value is wrapped into 0..1, so 1.25
       *         and -0.75 both mean 0.25. The next sample produced is this
       *         phase.
       */
      phasor& phase(Flt value);

      /** @brief The phase the next sample will have, in 0..1. */
      Flt phase() const;

    private:
      // The frequency or period this block starts with: @p wanted unless sync
      // update holds the one in force (@p held) until the ramp wraps.
      Dbl Take(Dbl& held, Dbl wanted, bool& known);

      Dbl current;
      buffer output;

      // Sync update: the frequency (Hz) and period (beats) in force, and
      // whether each has been taken yet.
      bool syncOn = false;
      bool takeNow = false;
      Dbl heldFrequency = 0.0;
      Dbl heldPeriod = 0.0;
      bool haveFrequency = false;
      bool havePeriod = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASOR_HPP
