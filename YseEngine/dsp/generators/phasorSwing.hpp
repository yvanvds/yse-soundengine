/*
  ==============================================================================

    phasorSwing.hpp
    Splits each cycle of a 0..1 phase ramp into two unequal ramps (issue
    #1076).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORSWING_HPP
#define YSE_DSP_GENERATORS_PHASORSWING_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Splits each cycle of a 0..1 ramp (a ``phasor`` output) into
     *         two full ramps of unequal length: swing on a phasor clock.
     *
     *  Ported from Max's ``swing~``. The **swing** is the share of the input
     *  cycle the first ramp takes; the second ramp takes the rest. For input
     *  ``x`` and swing ``s`` the output is ``x / s`` while ``x < s`` and
     *  ``(x - s) / (1 - s)`` from there on, sample-accurate and in phase with
     *  the input. 0.5 gives two equal ramps; 0.75 a first ramp three quarters
     *  of the cycle long and a second of one quarter. The swing is clamped to
     *  0.15 .. 0.85, as in Max; a value that is not a finite number is
     *  ignored.
     *
     *  **When a new swing lands.** With ``syncUpdate()`` off (the default) a
     *  new swing applies on the next sample, so the output can jump. With it
     *  on, the new swing waits for the input to wrap. Max says a change
     *  waits for "the input or the output" to reset; the output's own reset
     *  in the middle of the cycle is not used here, because a new swing
     *  landing there would move the start of the second ramp, which has
     *  already begun, and make the output jump. So with ``syncUpdate()`` on
     *  every ramp keeps the length it started with. A swing set before the
     *  first sample applies from it, and turning ``syncUpdate()`` off lands
     *  a waiting swing on the next sample.
     *
     *  The input is wrapped into 0..1; a sample that is not a finite number
     *  counts as the previous one (0 before any). A fall of more than half a
     *  cycle is read as a forward wrap and a rise of more than half as a
     *  backward one, so a falling ramp gives two falling ramps and lands a
     *  waiting swing where it wraps. Nothing depends on the sample rate.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API phasorSwing {
    public:
      /** @brief The smallest swing, as in Max. */
      static constexpr Flt MIN_SWING = 0.15f;
      /** @brief The largest swing, as in Max. */
      static constexpr Flt MAX_SWING = 0.85f;

      phasorSwing();

      /** @brief The share of the input cycle taken by the first ramp,
       *         clamped to ``MIN_SWING`` .. ``MAX_SWING``; a non-finite value
       *         is ignored. Lands as ``syncUpdate()`` says. */
      phasorSwing& swing(Flt value);
      /** @brief The last swing set, landed or not. */
      Flt swing() const {
        return requested;
      }

      /** @brief On: a new swing waits for the input to wrap. Off (the
       *         default): it lands on the next sample. */
      phasorSwing& syncUpdate(bool on);
      bool syncUpdate() const {
        return sync;
      }

      /** @brief One sample for input ``phase``. */
      Flt process(Flt phase);

      /** @brief One block. */
      buffer& operator()(buffer& phase);

    private:
      Flt Step(Flt in);

      buffer output;
      Flt requested = 0.5f;
      Flt active = 0.5f;
      bool sync = false;

      // The (wrapped) input read last, and whether there was one.
      Flt previous = 0.f;
      bool started = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORSWING_HPP
