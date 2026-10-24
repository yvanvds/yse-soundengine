/*
  ==============================================================================

    stepFunction.hpp
    A breakpoint function played one segment per cycle of an input phasor
    (issue #1081).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_STEPFUNCTION_HPP
#define YSE_DSP_GENERATORS_STEPFUNCTION_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A breakpoint function whose segments are timed by the cycles
     *         of an input ramp: each cycle of a ``phasor`` plays one segment,
     *         for stepped modulation and pitch sequences.
     *
     *  Ported from Max's ``stepfun~``. The **pattern** is a list of values,
     *  the breakpoints. **Step** ``k`` travels from value ``k - 1`` to value
     *  ``k`` over one cycle of the input, the input's phase (0..1) being how
     *  far along it is; step 0 travels from the last value to the first, so
     *  the pattern is a loop. With the pattern ``0 1 0.5`` and a rising
     *  phasor as the input, the output ramps 0.5 to 0, 0 to 1, 1 to 0.5, and
     *  over again. Because the input's phase drives the segment, a bent input
     *  ramp bends the output in the same way.
     *
     *  This numbering matches ``phasorStepDiv``'s index, the number of the
     *  breakpoint its ramp travels to: feed ``phasorStepDiv``'s ramp and
     *  index to ``operator()(buffer&, buffer&)`` and the function plays one
     *  segment per step with the times ``phasorStepDiv`` was given. (Max
     *  leaves the numbering open; this is the reading here.)
     *
     *  **Which step plays.** By default the input's own cycles count the
     *  steps: the step goes up by one each time the input wraps forward
     *  (from near 1 to near 0, a rising phasor) and down by one when it
     *  wraps backward (a falling one), modulo the pattern's length. A fall
     *  of more than half a cycle is read as a forward wrap, a rise of more
     *  than half as a backward one. ``step()`` sets the step from the next
     *  sample on (wrapped into the pattern, so -1 is the last step) and
     *  ``reset()`` sets it back to 0. With a step signal instead
     *  (``process(phase, step)``, ``operator()(buffer&, buffer&)``), the step
     *  is that signal's value, rounded down and wrapped into the pattern;
     *  the input's wraps then count nothing and ``step()`` has no effect on
     *  the output.
     *
     *  **Curves.** ``curvePattern()`` gives each step a curve, -1..1, as
     *  ``.function`` and ``shapeEnvelope`` bend their segments: the segment
     *  is ``from + (to - from) * phase ^ ((1 + c) / (1 - c))``, so 0 is
     *  linear, a positive curve moves late and a negative one early. Steps
     *  without a curve are linear. A curve that is not a finite number
     *  counts as 0.
     *
     *  **The pattern** holds up to ``MAX_STEPS`` values; more are ignored,
     *  as are values that are not finite numbers. One value is a constant;
     *  an empty pattern (the default) outputs 0. ``breakpoints()`` takes the
     *  values (and curves) from a list in line format, as a breakpoint
     *  function sends it: ``value time`` pairs, or ``value time curve``
     *  triples; the times are ignored, the input sets the timing. Max's
     *  convention of an initial value before the list is the optional
     *  ``initial``.
     *
     *  **When a change lands.** ``syncUpdate()`` says when a new pattern or
     *  curve pattern takes effect:
     *
     *  - ``IMMEDIATE`` (the default): on the next sample. The step playing
     *    keeps its number, wrapped into the new length.
     *  - ``INPUT_RESET``: on the next sample at which the input wraps, in
     *    either direction.
     *  - ``SEQUENCE_END``: when the sequence completes: counting the input's
     *    wraps, when the step goes round the end of the pattern (forward
     *    from the last step to the first, or backward from the first to the
     *    last); with a step signal, when the step it gives falls.
     *
     *  Whatever is set before the first sample, and a waiting pattern at a
     *  ``reset()``, takes effect at once.
     *
     *  The input is wrapped into 0..1; a sample of the input or the step
     *  signal that is not a finite number counts as the one before it.
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor and the pattern lives in
     *  fixed arrays.
     */
    class API stepFunction {
    public:
      /** @brief The capacity of the pattern. */
      static constexpr UInt MAX_STEPS = 64;

      /** @brief When a new pattern takes effect; see the class
       *         description. */
      enum SYNC {
        IMMEDIATE = 0,
        INPUT_RESET = 1,
        SEQUENCE_END = 2,
      };

      stepFunction();

      /** @brief Replaces the values with ``count`` values (``nullptr`` or 0
       *         empties the pattern), when ``syncUpdate()`` says. The curves
       *         are kept. */
      stepFunction& pattern(const Flt* values, UInt count);

      /** @brief Replaces the curves, one per step (-1..1, 0 linear), when
       *         ``syncUpdate()`` says; steps past ``count`` are linear. */
      stepFunction& curvePattern(const Flt* curves, UInt count);

      /** @brief Values and curves from a list in line format: ``value
       *         time`` pairs, or ``value time curve`` triples with
       *         ``curves`` on (an incomplete last group is dropped). With
       *         ``initial``, that value comes first (with curve 0 when
       *         ``curves`` is on). Triples replace the whole curve pattern;
       *         pairs carry no curves and keep it, as in Max. */
      stepFunction& breakpoints(const Flt* list, UInt count, bool curves,
                                const Flt* initial = nullptr);

      /** @brief When a new pattern takes effect. */
      stepFunction& syncUpdate(SYNC mode);

      /** @brief Counting the input's wraps: play step ``n`` (wrapped into
       *         the pattern) from the next sample on. */
      stepFunction& step(int n);

      /** @brief Counting the input's wraps: back to step 0; a waiting
       *         pattern takes effect. */
      stepFunction& reset();

      /** @brief The number of values in the pattern in use (not a waiting
       *         one). */
      UInt length() const {
        return active.count;
      }

      /** @brief The step of the last sample processed. */
      UInt lastStep() const {
        return current;
      }

      /** @brief One sample for input ``phase``, counting its wraps. */
      Flt process(Flt phase);

      /** @brief One sample for input ``phase`` at step ``stepIn``. */
      Flt process(Flt phase, Flt stepIn);

      /** @brief One block for the input ``phase``, counting its wraps. */
      buffer& operator()(buffer& phase);

      /** @brief One block for the input ``phase``, at the steps given by
       *         ``stepIn`` (the shorter of the two sets the length). */
      buffer& operator()(buffer& phase, buffer& stepIn);

    private:
      struct table {
        Flt values[MAX_STEPS] = {};
        UInt count = 0;
        Flt curves[MAX_STEPS] = {};
        // Derived when the table lands: the exponent of each step's curve.
        Flt exponents[MAX_STEPS] = {};
      };

      int Wrap(Flt phase);
      void Request();
      void Land();
      Flt Value(Dbl f) const;
      Flt Counted(Flt phase);
      Flt Stepped(Flt phase, Flt stepIn);

      buffer output;

      table active;
      table requested;
      bool pending = false;
      SYNC sync = IMMEDIATE;

      Flt previous = 0.f; // the last input read, wrapped
      bool started = false; // a sample read since construction
      UInt current = 0;
      long long lastIndex = 0; // the step signal's last whole value
      Flt lastStepIn = 0.f; // the step signal's last finite value
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_STEPFUNCTION_HPP
