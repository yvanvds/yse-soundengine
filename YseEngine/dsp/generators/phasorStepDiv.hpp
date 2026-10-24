/*
  ==============================================================================

    phasorStepDiv.hpp
    One 0..1 ramp per step of a stepped function, over the cycle of an input
    ramp or over times in milliseconds (issue #1075).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORSTEPDIV_HPP
#define YSE_DSP_GENERATORS_PHASORSTEPDIV_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A fresh 0..1 ramp for each step of a stepped function: per-step
     *         envelopes in a sequence.
     *
     *  Ported from Max's ``stepdiv~``. A **pattern** is a list of step
     *  times; every step plays one full 0..1 ramp, and a second output, the
     *  **index**, is the number of the step playing (its position in the
     *  pattern, 0 .. steps - 1). Feed both to a function lookup to play the
     *  function one segment at a time.
     *
     *  **Driven** (``process(phase)``, ``operator()(buffer&)``): each cycle
     *  of an input ramp (a ``phasor`` output) is cut into the steps, each
     *  taking its time's share of the total. Times 50 and 25 play a ramp
     *  over the first two thirds of the input cycle and one over the last
     *  third. Sample-accurate and in phase with the input. Without a pattern
     *  (or with every time 0) the input passes through with index 0.
     *
     *  **Free running** (``process()``, ``operator()()``): the times are in
     *  milliseconds. ``start()`` plays the steps from the first one; at the
     *  end, ``loop()`` on starts again, off stops. Stopped, the output holds:
     *  0 with index 0 before the first start, and 1 with the index of the
     *  last step after a sequence ends, so a function lookup holds its last
     *  value. ``step(n)`` jumps to the start of step ``n`` (clamped into the
     *  pattern) and plays on from there, started or not. Times are converted
     *  with the live ``SAMPLERATE`` at every block (every call of
     *  ``process()``), and a step ends sample-accurately: the time left over
     *  carries into the next step.
     *
     *  **Steps of time 0** take no time and never play, but keep their
     *  place: with the line format a breakpoint function sends, ``0 0 1 500
     *  0 250``, the times are 0, 500 and 250, the first ramp has index 1 and
     *  the index is the number of the breakpoint the ramp travels to. A
     *  negative time or one that is not a finite number counts as 0.
     *
     *  **Three ways to give the times.** ``pattern()`` takes them as they
     *  are. ``breakpoints()`` takes a list in line format, ``value time``
     *  pairs or, with curves, ``value time curve`` triples, and uses the
     *  times (an incomplete last group is dropped). ``onsets()`` takes the
     *  moments at which the steps end, measured from the start of the
     *  pattern, over a span of 1 or the largest onset if that is larger:
     *  ``0.25 0.5 0.75`` is four steps of 0.25, the last one added to reach
     *  the end of the span, so the ramps restart at 25%, 50% and 75% of the
     *  input cycle; ``100 300`` is two steps, 100 and 200. An onset below
     *  the largest before it makes a step of time 0. Max leaves the span of
     *  its ``what`` message unspecified; this is the reading here.
     *
     *  **When a change lands.** With ``syncUpdate()`` off (the default) a
     *  new pattern lands on the next sample. Driven, the input is then read
     *  in the new pattern at once; free running, the step playing keeps its
     *  number and the time it has played, and ends at once if the new
     *  pattern makes it shorter than that or has no step of that number.
     *  With ``syncUpdate()`` on, a new pattern waits for the input to wrap
     *  (driven) or for the sequence to end, a ``start()`` or a ``step()``
     *  (free running). Whatever is set before the first sample, or while a
     *  free-running sequence is stopped, applies at once.
     *
     *  Driven, the input is wrapped into 0..1; a sample that is not a finite
     *  number counts as the previous one. A fall of more than half a cycle
     *  is read as a forward wrap, a rise of more than half as a backward
     *  one, so a falling ramp works too. Driven output does not depend on
     *  the sample rate.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the outputs
     *  are sized to ``STANDARD_BUFFERSIZE`` in the constructor and the
     *  pattern lives in fixed arrays.
     */
    class API phasorStepDiv {
    public:
      /** @brief The most steps in a pattern; extra times are dropped. */
      static constexpr UInt MAX_STEPS = 64;

      phasorStepDiv();

      /** @brief The step times as they are: relative shares of the input
       *         cycle when driven, milliseconds when free running.
       *         ``count`` 0 (or a null ``times``) clears the pattern. Lands
       *         as ``syncUpdate()`` says. */
      phasorStepDiv& pattern(const Flt* times, UInt count);

      /** @brief The step times from a list in line format: ``value time``
       *         pairs, or ``value time curve`` triples with ``curves`` on. */
      phasorStepDiv& breakpoints(const Flt* list, UInt count, bool curves);

      /** @brief The step times from the moments the steps end, measured
       *         from the start (see the class notes for the span). Up to
       *         ``MAX_STEPS - 1`` onsets are read, leaving room for the step
       *         that completes the span. */
      phasorStepDiv& onsets(const Flt* offsets, UInt count);

      /** @brief The number of steps in the last pattern set, landed or not. */
      UInt patternLength() const {
        return requested.count;
      }
      /** @brief Time ``i`` of the last pattern set (0 past its end). */
      Flt time(UInt i) const {
        return i < requested.count ? requested.times[i] : 0.f;
      }

      /** @brief Free running: start again at the end of the sequence. */
      phasorStepDiv& loop(bool on);
      bool loop() const {
        return looping;
      }

      /** @brief On: a new pattern waits for the input to wrap, or for a
       *         free-running sequence to end or restart. */
      phasorStepDiv& syncUpdate(bool on);
      bool syncUpdate() const {
        return synced;
      }

      /** @brief Free running: play the steps from the first one, from the
       *         next sample on. Nothing plays without a step of a time above
       *         0. */
      phasorStepDiv& start();

      /** @brief Free running: play from the start of step ``n`` (clamped to
       *         the pattern), from the next sample on. */
      phasorStepDiv& step(int n);

      /** @brief Free running: whether a sequence is playing. */
      bool running() const {
        return playing;
      }

      /** @brief Driven: one sample of the step ramp for input ``phase``;
       *         the index of that sample is ``lastIndex()``. */
      Flt process(Flt phase);

      /** @brief Free running: one sample of the step ramp. */
      Flt process();

      /** @brief The index output of the last sample processed. */
      Flt lastIndex() const {
        return currentIndex;
      }

      /** @brief Driven: one block of step ramps; the index of the same
       *         block is in ``index()``. */
      buffer& operator()(buffer& phase);

      /** @brief Free running: ``STANDARD_BUFFERSIZE`` samples of step
       *         ramps; the index of the same block is in ``index()``. */
      buffer& operator()();

      /** @brief The index output of the last block processed. */
      buffer& index() {
        return indexOutput;
      }

    private:
      // A pattern, with the step starts and the total worked out when it
      // lands.
      struct layout {
        Flt times[MAX_STEPS] = {};
        UInt count = 0;
        // Derived when the layout lands:
        Dbl starts[MAX_STEPS] = {};
        Dbl total = 0.0;
      };

      Flt Driven(Flt in);
      Flt Free();
      void Land();
      void Begin(UInt from);
      void Settle();
      Dbl Length(UInt i) const;

      buffer output;
      buffer indexOutput;

      layout requested;
      layout active;
      bool pending = false;

      bool looping = false;
      bool synced = false;

      // Driven: the input read last.
      Flt previous = 0.f;
      bool started = false;

      // Free running: the step playing, the samples it has played, the
      // samples per millisecond of this block, and what a stopped sequence
      // holds.
      bool playing = false;
      UInt cursor = 0;
      Dbl elapsed = 0.0;
      Dbl samplesPerMs = 0.0;
      Flt heldOut = 0.f;
      Flt heldIndex = 0.f;

      Flt currentIndex = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORSTEPDIV_HPP
