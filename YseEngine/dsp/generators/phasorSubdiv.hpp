/*
  ==============================================================================

    phasorSubdiv.hpp
    Splits each cycle of a 0..1 phase ramp into whole-number sub-ramps
    (issue #1074).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORSUBDIV_HPP
#define YSE_DSP_GENERATORS_PHASORSUBDIV_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Splits each cycle of a 0..1 ramp (a ``phasor`` output) into a
     *         whole number of shorter ramps: tuplets and ratchets.
     *
     *  Ported from Max's ``subdiv~``. With ``div(n)`` the input cycle is cut
     *  into ``n`` equal steps and every step plays a full 0..1 ramp: for
     *  input ``x`` the output is ``frac(x n)``, sample-accurate and in phase
     *  with the input. A second output, the **index**, is the number of the
     *  step playing (0 .. n - 1).
     *
     *  **Duration pattern.** ``pattern()`` gives each step a length in
     *  subdivision units, cycling: with ``div(3)`` and the pattern ``1 2``
     *  the input cycle plays a ramp of 1/3 and then one of 2/3. A pattern
     *  whose total is not a multiple of ``n`` is padded with one last step
     *  that brings it to the next multiple (``div(5)``, ``1 2`` plays
     *  ``1 2 2``; ``div(4)``, ``1 2`` plays ``1 2 1``). A pattern longer
     *  than ``n`` units spans several input cycles and restarts with them:
     *  with ``div(3)``, ``2 1 1 2 1 1 1`` covers three input cycles. Max
     *  leaves a step that crosses an input wrap undefined; here it runs on
     *  through the wrap as one ramp (``div(3)``, ``2 2 2`` is three ramps
     *  over two input cycles). The index is then the step number in the
     *  pattern, 0 .. steps - 1. Input wraps are counted from the start, so
     *  the pattern cycle stays aligned with them.
     *
     *  **Probability pattern.** ``prob()`` gives each step a chance to play,
     *  cycling independently of the duration pattern: 1 always, 0 never, in
     *  between a random draw made once when the step starts. A step that
     *  does not play outputs a constant 0, and its index follows
     *  ``silentMode()``: ``hold`` keeps the index of the last step that
     *  played (0 before any), ``minusOne`` outputs -1. Which probability a
     *  step takes is set by ``lockProb()``: on, the probabilities count
     *  steps of the duration pattern (one per ramp); off (the default), they
     *  count subdivision units, so a probability stays with its position on
     *  the ``div`` grid and a long step takes the one its first unit lands
     *  on. Without a duration pattern both are the same. The random source
     *  is ``seed()``-able.
     *
     *  **When a change lands.** A new ``div()`` or ``pattern()`` takes
     *  effect according to ``syncUpdate()``: ``now`` (the default) on the
     *  next sample, ``output`` when the next output step starts, ``input``
     *  when the input next wraps. A change that lands mid-step starts a new
     *  step, with a new probability draw. Whatever is set before the first
     *  sample applies from it in every mode. ``prob()``, ``lockProb()`` and
     *  ``silentMode()`` apply at once (a probability at the next step).
     *
     *  The input is wrapped into 0..1; a sample that is not a finite number
     *  counts as the previous one. A fall of more than half a cycle is read
     *  as a forward wrap, a rise of more than half as a backward one, so a
     *  falling ramp works too. Nothing depends on the sample rate.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the outputs
     *  are sized to ``STANDARD_BUFFERSIZE`` in the constructor and the
     *  patterns live in fixed arrays.
     */
    class API phasorSubdiv {
    public:
      /** @brief The largest ``div()``, as in Max. */
      static constexpr int MAX_DIV = 16384;
      /** @brief The longest duration or probability pattern; extra values
       *         are dropped. */
      static constexpr UInt MAX_PATTERN = 64;

      /** @brief The index of a step that does not play: Max's
       *         ``silentmode`` values 0 and 1. */
      enum class silent {
        hold = 0, ///< the index of the last step that played (the default)
        minusOne = 1, ///< -1
      };

      /** @brief When a new ``div()`` or ``pattern()`` lands: Max's
       *         ``syncupdate`` values 0, 1 and 2. */
      enum class update {
        now = 0, ///< on the next sample (the default)
        output = 1, ///< when the next output step starts
        input = 2, ///< when the input next wraps
      };

      phasorSubdiv();

      /** @brief The number of subdivisions, clamped to 1 .. ``MAX_DIV``.
       *         Lands as ``syncUpdate()`` says. */
      phasorSubdiv& div(int value);
      /** @brief The last ``div()`` set, landed or not. */
      int div() const {
        return requested.subdivisions;
      }

      /** @brief The duration pattern, ``count`` step lengths in subdivision
       *         units (below 1 counts as 1, above ``MAX_DIV`` as
       *         ``MAX_DIV``). ``count`` 0 clears it: every step one unit.
       *         Lands as ``syncUpdate()`` says. */
      phasorSubdiv& pattern(const int* steps, UInt count);
      /** @brief The number of values in the last ``pattern()`` set. */
      UInt patternLength() const {
        return requested.count;
      }

      /** @brief The probability pattern, ``count`` chances 0..1 (clamped; a
       *         value that is not a finite number counts as 0). ``count`` 0
       *         clears it: every step plays. */
      phasorSubdiv& prob(const Flt* chances, UInt count);
      /** @brief The number of values in the probability pattern. */
      UInt probLength() const {
        return probCount;
      }

      /** @brief On: one probability per duration step; off: per
       *         subdivision unit. */
      phasorSubdiv& lockProb(bool on);
      bool lockProb() const {
        return lockProbability;
      }

      /** @brief The index output of a step that does not play. */
      phasorSubdiv& silentMode(silent value);
      silent silentMode() const {
        return silence;
      }

      /** @brief When a new ``div()`` or ``pattern()`` lands. */
      phasorSubdiv& syncUpdate(update value);
      update syncUpdate() const {
        return landing;
      }

      /** @brief Restart the random source for the probability draws. */
      phasorSubdiv& seed(std::uint32_t value);

      /** @brief One sample of the sub-ramp for input ``phase``; the index
       *         of that sample is ``lastIndex()``. */
      Flt process(Flt phase);

      /** @brief The index output of the last sample processed. */
      Flt lastIndex() const {
        return currentIndex;
      }

      /** @brief One block of sub-ramps; the index of the same block is in
       *         ``index()``. */
      buffer& operator()(buffer& phase);

      /** @brief The index output of the last block processed. */
      buffer& index() {
        return indexOutput;
      }

    private:
      // A subdivision and duration pattern, with the padding step and the
      // step starts worked out when it lands.
      struct layout {
        int subdivisions = 1;
        int steps[MAX_PATTERN + 1] = {};
        UInt count = 0; // as given, before padding
        // Derived when the layout lands:
        int starts[MAX_PATTERN + 1] = {};
        UInt used = 0; // steps with padding; 0 for no pattern
        std::int64_t units = 1; // pattern length in subdivision units
        std::int64_t cycles = 1; // pattern length in input cycles
      };

      Flt Step(Flt in);
      void Land();

      buffer output;
      buffer indexOutput;

      layout requested;
      layout active;
      bool pending = false;

      Flt chance[MAX_PATTERN] = {};
      UInt probCount = 0;
      bool lockProbability = false;
      silent silence = silent::hold;
      update landing = update::now;
      std::uint32_t state;

      // The input read last, the input wraps counted since the start, the
      // step playing, whether it plays, and the index output.
      Flt previous = 0.f;
      bool started = false;
      std::int64_t wraps = 0;
      std::int64_t stepId = 0;
      bool hasStep = false;
      bool playing = true;
      Flt playedIndex = 0.f;
      Flt currentIndex = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORSUBDIV_HPP
