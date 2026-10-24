/*
  ==============================================================================

    stepCounter.hpp
    Counts jumps in a signal, such as the resets of a phasor, through a
    sequence of counts (issue #1080).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_STEPCOUNTER_HPP
#define YSE_DSP_GENERATORS_STEPCOUNTER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Counts the jumps in a signal (the resets of a ``phasor``, the
     *         impulses of a ``click``) through a sequence of counts, and sends
     *         an impulse each time a count is reached: a sequence of
     *         durations in beats from a steady clock.
     *
     *  Ported from Max's ``stepcounter~``. With the sequence ``2 1 3`` and a
     *  phasor as the clock, the impulses are 2 cycles apart, then 1, then 3,
     *  and over again.
     *
     *  **A jump** is a difference between two successive samples of at least
     *  the threshold (``threshold()``, default 0.75), so a phasor's reset from
     *  near 1 to near 0 is one. ``direction()`` says which jumps count:
     *  ``BOTH`` (the default), ``UP`` only (a rise, as the reset of a phasor
     *  with a negative frequency) or ``DOWN`` only (a fall, as the reset of a
     *  phasor with a positive frequency). An impulse rises and falls, so it
     *  is two jumps in ``BOTH``: count impulses with ``UP``. Before the first
     *  sample the input reads as 0, so an impulse on the first sample counts.
     *  A sample that is not a finite number is no jump and is skipped: the
     *  next difference is taken from the sample before it.
     *
     *  **Counting.** Each jump adds one to the count of the step playing;
     *  when the count reaches the step's value the impulse output is 1 for
     *  that sample, the count goes back to 0 and the next step plays, the
     *  last one followed by the first. What the first jump after a start or
     *  a ``reset()`` does depends on ``startMode()`` (the reference leaves
     *  the details open; this is the reading here):
     *
     *  - on (the default): the first jump is where the sequence starts. It
     *    gives an impulse and counts nothing, so ``2 1 3`` fires at jumps
     *    0, 2, 3, 6, 8 ... from there.
     *  - off: the sequence starts at the start or the reset itself, and the
     *    first jump is the first counted, so ``2 1 3`` fires at jumps 2, 3,
     *    6, 8 ... (numbered from 1), the first impulse being the end of the
     *    first step.
     *
     *  Next to the impulses there are two more outputs: the count of the step
     *  playing (0 just after an impulse, up to the step's value less one) and
     *  the step's index in the sequence (``counts()`` and ``steps()``, or
     *  ``lastCount()`` and ``lastStep()`` sample by sample).
     *
     *  **The sequence** holds up to ``MAX_STEPS`` counts; more are ignored,
     *  as are values that are not finite numbers. Each is rounded to a whole
     *  number, at least 1. An empty sequence (the default) counts every jump
     *  as a step of 1. ``sequence()`` replaces it, and ``syncUpdate()`` says
     *  when a new one takes effect:
     *
     *  - ``IMMEDIATE`` (the default): at once. The step index is kept (or
     *    wrapped into the new length) and so is the count, so a step whose
     *    new value is already reached ends at the next jump.
     *  - ``AFTER_STEP``: at the next impulse, the end of the step playing;
     *    the next step is the same index of the new sequence (or wrapped).
     *  - ``AFTER_SEQUENCE``: at the impulse that ends the last step, so the
     *    new sequence starts at its first step.
     *
     *  Until the first jump after a start or a reset no step has begun, so a
     *  waiting sequence takes effect at once, as it does at a ``reset()``.
     *  ``reset()`` sets the count and the step to 0 and waits for a first
     *  jump again; it leaves the input history alone, so it does not invent
     *  a jump.
     *
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, the sequence is a fixed
     *  array.
     */
    class API stepCounter {
    public:
      /** @brief The capacity of the sequence. */
      static constexpr UInt MAX_STEPS = 64;

      /** @brief Which jumps count; see the class description. */
      enum DIRECTION {
        BOTH = 0,
        UP = 1,
        DOWN = 2,
      };

      /** @brief When a new sequence takes effect; see the class
       *         description. */
      enum SYNC {
        IMMEDIATE = 0,
        AFTER_STEP = 1,
        AFTER_SEQUENCE = 2,
      };

      stepCounter();

      /** @brief Replaces the sequence with ``count`` counts (``nullptr`` or
       *         0 empties it), when ``syncUpdate()`` says. */
      stepCounter& sequence(const Flt* counts, UInt count);

      /** @brief Which jumps count. */
      stepCounter& direction(DIRECTION d);

      /** @brief On: the first jump after a start or a reset gives an impulse
       *         and starts the sequence. Off: it is the first one counted. */
      stepCounter& startMode(bool on);

      /** @brief When a new sequence takes effect. */
      stepCounter& syncUpdate(SYNC mode);

      /** @brief The smallest difference between two successive samples that
       *         is a jump; one that is not a finite number is ignored. 0 or
       *         less: any change. */
      stepCounter& threshold(Flt value);

      /** @brief The count and the step back to 0, waiting for a first jump;
       *         a waiting sequence takes effect. */
      stepCounter& reset();

      /** @brief The number of counts in the sequence in use (not a waiting
       *         one). */
      UInt length() const {
        return active.count;
      }

      /** @brief One sample for input ``in``: 1 on an impulse, else 0. The
       *         count and step of that sample are ``lastCount()`` and
       *         ``lastStep()``. */
      Flt process(Flt in);

      /** @brief The count output of the last sample processed. */
      Flt lastCount() const {
        return static_cast<Flt>(counted);
      }

      /** @brief The step output of the last sample processed. */
      Flt lastStep() const {
        return static_cast<Flt>(current);
      }

      /** @brief One block of impulses for the input ``in``; the count and
       *         step of the same block are in ``counts()`` and ``steps()``. */
      buffer& operator()(buffer& in);

      /** @brief The count output of the last block processed. */
      buffer& counts() {
        return countOutput;
      }

      /** @brief The step output of the last block processed. */
      buffer& steps() {
        return stepOutput;
      }

    private:
      struct sequenceList {
        int values[MAX_STEPS] = {};
        UInt count = 0;
      };

      Flt Step(Flt in);
      void Land();
      int Target() const;

      buffer output;
      buffer countOutput;
      buffer stepOutput;

      sequenceList active;
      sequenceList requested;
      bool pending = false;
      SYNC sync = IMMEDIATE;
      DIRECTION dir = BOTH;
      bool startImpulse = true;
      Flt thresh = 0.75f;

      Flt previous = 0.f; // the last finite input
      bool started = false; // a jump seen since the start or a reset
      int counted = 0;
      UInt current = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_STEPCOUNTER_HPP
