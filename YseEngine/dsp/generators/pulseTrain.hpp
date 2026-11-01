/*
  ==============================================================================

    pulseTrain.hpp
    A 0/1 pulse train given by period, width and phase (issue #1059).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PULSETRAIN_HPP
#define YSE_DSP_GENERATORS_PULSETRAIN_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A rectangular pulse train switching between 0 and 1, with its
     *         period in milliseconds: clocks, gates and rhythmic modulation.
     *
     *  Each period is one cycle. The output is 1 (the
     *  "on" part) for the first ``width`` of the cycle, counted from the
     *  ``phase`` offset, and 0 for the rest. A phase of 0 puts the on part at
     *  the start of the cycle; other values (1 is the same as 0) delay it by
     *  that fraction of the period.
     *
     *  The width is held within 0..1 and then so that each cycle keeps at
     *  least one sample on and one sample off: a width of 0 is a single
     *  sample at 1, a width of 1 is the whole period but one sample. A width
     *  that is not a number is 0.5, a phase that is not a number is 0. A
     *  period shorter than two samples cannot alternate cleanly; at or below
     *  one sample the output stays at 1.
     *
     *  An interval of 0 or less (or not a number) stops the train: the phase
     *  holds and so does the output. The period is turned into samples with
     *  the live ``SAMPLERATE``, read on every sample, so the train keeps its
     *  tempo across an engine restart at another rate. The position in the
     *  cycle is counted in samples, in double precision: a period of a whole
     *  number of samples is exact, and a fractional one does not drift.
     *
     *  By default a new interval takes effect at once, from the current
     *  position in the cycle. With ``deferInterval(true)`` (``~train``'s
     *  ``resetmode 1``) the interval is read only when a cycle starts, so a
     *  change waits for the running cycle to finish. A stopped train adopts
     *  a new interval at once in either mode.
     *
     *  An onset is a sample at 1 after one at 0, the first sample at 1 after
     *  construction included. A block call lists the sample index of every
     *  onset in it, until the next block call. ``trigger()`` restarts the
     *  cycle so that the on part begins at the first sample of the next block
     *  call, an onset whatever the output was.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor and a
     *  block is at most that long (a longer one is processed up to that
     *  length). The onset list is a fixed array inside the object.
     */
    class API pulseTrain {
    public:
      pulseTrain();

      /** @brief One block from the set interval, width and phase. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample interval in milliseconds,
       *         with the set width and phase. */
      buffer& operator()(buffer& periods);

      /** @brief One block driven by a per-sample interval in milliseconds,
       *         width and phase. The block is as long as the shortest of the
       *         three. */
      buffer& operator()(buffer& periods, buffer& widths, buffer& phases);

      /** @brief Onsets in the last block call. */
      UInt onsets() const;

      /** @brief The sample index of onset @p i in the last block call, in
       *         order; @p i must be below ``onsets()``. */
      UInt onset(UInt i) const;

      /** @brief Sets the period in milliseconds; 0 or less stops the train.
       *         Default 1000. */
      pulseTrain& interval(Flt ms);

      /** @brief The period in milliseconds. */
      Flt interval() const;

      /** @brief Sets the part of the period at 1, 0..1. Default 0.5. */
      pulseTrain& width(Flt value);

      /** @brief The pulse width. */
      Flt width() const;

      /** @brief Sets the onset delay as a fraction of the period, wrapped
       *         into 0..1. Default 0. */
      pulseTrain& phase(Flt value);

      /** @brief The phase offset, in 0..1. */
      Flt phase() const;

      /** @brief True defers an interval change to the start of the next
       *         cycle; false (the default) applies it at once. */
      pulseTrain& deferInterval(bool on);

      /** @brief True when an interval change waits for the next cycle. */
      bool deferInterval() const;

      /** @brief Starts the on part at the first sample of the next block
       *         call: an onset there, and a new cycle from it. */
      pulseTrain& trigger();

      /** @brief Back to the state after construction: the cycle at its
       *         start, the output as if it had been 0, no onsets. Keeps the
       *         interval, width, phase and mode. */
      pulseTrain& reset();

    private:
      Flt step(Flt ms, Flt w, Flt ph, UInt index);
      void retime(Dbl target);

      Flt intervalMs;
      Flt pulseWidth;
      Flt offset;
      bool defer;
      bool kick;
      bool cycleStart; // the next sample starts a cycle (deferred mode)
      bool on; // the last sample was at 1
      bool running; // false while stopped
      Dbl elapsed; // samples into the cycle of the next sample
      Dbl period; // samples per cycle; kept while stopped
      UInt count;
      UInt pos[STANDARD_BUFFERSIZE];
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PULSETRAIN_HPP
