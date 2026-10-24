/*
  ==============================================================================

    phasorWhere.hpp
    The time elapsed in, and remaining of, the current cycle of a 0..1 phase
    ramp (issue #1078).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORWHERE_HPP
#define YSE_DSP_GENERATORS_PHASORWHERE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Reports, as two signals in milliseconds, how long ago a 0..1
     *         ramp (a ``phasor`` output) last started its cycle and how long
     *         it will take to finish it: time-aware events inside a cycle.
     *
     *  Ported from Max's ``where~``. To act 30 ms before the ramp resets,
     *  wait for the **remaining** time to drop to 30 or below.
     *
     *  **Elapsed** is measured time: the samples since the ramp last
     *  wrapped, counted from the point between two samples where it crossed
     *  the wrap (the step is assumed straight), so it is 0 only when a
     *  sample lands exactly on the reset. It keeps counting when the ramp
     *  slows down, speeds up or stops.
     *
     *  **Remaining** is a prediction: the part of the cycle still to go
     *  divided by the speed of the last input step that moved. It follows a
     *  change of speed on the next sample. While the input holds still it
     *  keeps the last prediction (the reference leaves a stopped ramp
     *  unspecified; a value that never runs out would be useless as a
     *  trigger). For a ramp of constant speed, elapsed plus remaining is the
     *  length of a cycle.
     *
     *  **Falling ramps** (a ``phasor`` with a negative frequency) are
     *  measured the same way: the direction is that of the last step that
     *  moved, and a falling ramp's cycle starts at 1 and ends at 0. When the
     *  direction changes without a wrap, or on the first step that moves,
     *  there is no reset to count from: elapsed is then estimated as the part
     *  of the cycle already done divided by the speed of that step, and
     *  counted on from there. Before the input first moves both outputs are
     *  0.
     *
     *  The input is wrapped into 0..1; a sample that is not a finite number
     *  counts as the previous one (0 before any), so it reads as the input
     *  holding still. A fall of more than half a cycle is read as a forward
     *  wrap and a rise of more than half as a backward one: a ramp faster
     *  than half the sample rate cannot be measured. The speed comes from a
     *  single step of a float input, so for slow ramps the prediction
     *  carries the rounding of that step (about 0.3% of the cycle at 1 Hz
     *  and 48 kHz).
     *
     *  Times are counted in samples and converted to milliseconds with the
     *  live ``SAMPLERATE`` at every block (every call of ``process()``).
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the outputs
     *  are sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API phasorWhere {
    public:
      phasorWhere();

      /** @brief One sample for input ``phase``: returns the elapsed time in
       *         ms; the remaining time of that sample is ``lastRemaining()``. */
      Flt process(Flt phase);

      /** @brief The remaining time, in ms, of the last sample processed. */
      Flt lastRemaining() const {
        return currentRemaining;
      }

      /** @brief One block: returns the elapsed time in ms; the remaining
       *         time of the same block is in ``remaining()``. */
      buffer& operator()(buffer& phase);

      /** @brief The remaining time, in ms, of the last block processed. */
      buffer& remaining() {
        return remainingOutput;
      }

    private:
      // One input sample; elapsed and remaining in samples.
      void Step(Flt in, Dbl& elapsedOut, Dbl& remainingOut);
      Dbl Progress(Flt x) const;

      buffer elapsedOutput;
      buffer remainingOutput;

      // The (wrapped) input read last, whether there was one, the direction
      // of the last step that moved (+1 rising, -1 falling) and its speed in
      // cycles per sample (0 before the input first moves).
      Flt previous = 0.f;
      bool started = false;
      int direction = 1;
      Dbl speed = 0.0;

      // Samples since the cycle started.
      Dbl elapsed = 0.0;

      Flt currentRemaining = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORWHERE_HPP
