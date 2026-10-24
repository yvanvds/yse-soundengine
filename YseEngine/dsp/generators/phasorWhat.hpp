/*
  ==============================================================================

    phasorWhat.hpp
    An impulse each time a 0..1 phase ramp passes one of a list of phase
    values (issue #1079).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORWHAT_HPP
#define YSE_DSP_GENERATORS_PHASORWHAT_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Sends an impulse, a 1 for one sample, each time a 0..1 ramp (a
     *         ``phasor`` output) passes one of a list of phase values:
     *         rhythmic triggers on a phasor clock.
     *
     *  Ported from Max's ``what~``. With the values ``0 0.25 0.5 0.75`` a
     *  phasor gives four evenly spaced impulses per cycle; with ``0 0.375
     *  0.75`` a 3 + 3 + 2 rhythm.
     *
     *  **Matching** follows the trigger mode (``triggerMode()``, default
     *  ``BOTH``):
     *
     *  - ``ASCENDING``: the previous sample is at or below the value and the
     *    current one above it.
     *  - ``DESCENDING``: the previous sample is at or above the value and the
     *    current one below it.
     *  - ``BOTH``: either of the two, so a ramp rising or falling (a phasor
     *    with a positive or a negative frequency) triggers alike.
     *  - ``EQUALS``: the current sample is exactly the value, at every such
     *    sample. A ramp seldom lands on a value exactly, so this suits
     *    stepped inputs better than ramps.
     *
     *  So in the first three modes a sample that lands exactly on a value
     *  fires on the sample after it, the one that moves on from it.
     *
     *  The input is a phase: it is wrapped into 0..1, and a jump of more than
     *  half a cycle is read as a wrap the short way round, so the reset of a
     *  rising ramp from 0.98 to 0.01 is a rise of 0.03 through 1 = 0 (it
     *  passes the value 0) and not a fall through every value. The values
     *  are phases too: each is wrapped into 0..1, so 1 is the same as 0 and
     *  fires at the reset. A ramp faster than half the sample rate cannot be
     *  followed. Several values passed by one sample give one impulse, of 1.
     *
     *  The first sample, which has no previous one, fires only in
     *  ``EQUALS`` mode. A sample that is not a finite number gives no impulse
     *  and is skipped: the next step is measured from the sample before it.
     *
     *  **The list** holds up to ``MAX_MATCHES`` values; more are ignored, as
     *  are values that are not finite numbers. ``matches()`` replaces it and
     *  ``clear()`` empties it. With ``syncUpdate(true)`` a new list waits for
     *  the input to wrap (a ramp's reset, in either direction) and takes
     *  effect at that sample, so the values of the new list are checked from
     *  the reset on; a list waiting when sync is switched off lands at the
     *  next sample. The very first sample processed always takes a waiting
     *  list, as there is no cycle to finish yet.
     *
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, the list is a fixed array.
     */
    class API phasorWhat {
    public:
      /** @brief The capacity of the value list. */
      static constexpr UInt MAX_MATCHES = 64;

      /** @brief How a value is matched; see the class description. */
      enum TRIGGER_MODE {
        ASCENDING = 0,
        DESCENDING = 1,
        BOTH = 2,
        EQUALS = 3,
      };

      phasorWhat();

      /** @brief Replaces the value list with ``count`` values (``nullptr`` or
       *         0 empties it). With sync on, the new list waits for a wrap of
       *         the input. */
      phasorWhat& matches(const Flt* values, UInt count);

      /** @brief Empties the value list; with sync on, at the next wrap. */
      phasorWhat& clear() {
        return matches(nullptr, 0);
      }

      /** @brief How values are matched. */
      phasorWhat& triggerMode(TRIGGER_MODE mode);

      /** @brief On: a new list waits for the input to wrap. */
      phasorWhat& syncUpdate(bool on);

      /** @brief The number of values in the list in use (not a waiting one). */
      UInt count() const {
        return active.count;
      }

      /** @brief One sample for input ``phase``: 1 on an impulse, else 0. */
      Flt process(Flt phase);

      /** @brief One block of impulses for the input ramp ``phase``. */
      buffer& operator()(buffer& phase);

    private:
      struct valueList {
        Flt values[MAX_MATCHES] = {};
        UInt count = 0;
      };

      Flt Step(Flt in);
      void Land();

      buffer output;

      valueList active;
      valueList requested;
      bool pending = false;
      bool synced = false;
      TRIGGER_MODE mode = BOTH;

      // The (wrapped) input read last and whether there was one.
      Flt previous = 0.f;
      bool started = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORWHAT_HPP
