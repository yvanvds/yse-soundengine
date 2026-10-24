/*
  ==============================================================================

    oscillatorBank.hpp
    A bank of sine oscillators with smoothed frequency and magnitude changes,
    for additive synthesis and resynthesis (issue #1061).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_OSCILLATORBANK_HPP
#define YSE_DSP_GENERATORS_OSCILLATORBANK_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A bank of table-lookup oscillators, summed into one signal:
     *         additive synthesis, and resynthesis from analysis data.
     *
     *  Ported from Max's ``oscbank~`` (non-interpolating) and ``ioscbank~``
     *  (interpolating), one class with a flag. Each oscillator has a
     *  frequency in Hz, a magnitude (its amplitude) and a running phase; the
     *  output is the sum of every active oscillator's ``magnitude *
     *  table(phase)``. The table is one cycle of a sine (``sin(2 pi phase)``,
     *  so an oscillator starts at 0), shared by all the oscillators of the
     *  bank, or a waveform given with ``setTable``.
     *
     *  ### Size
     *
     *  The **capacity**, the most oscillators the bank can run, is given to
     *  the constructor, which allocates every oscillator's state. ``size``
     *  sets how many of them run (oscillators ``0`` to ``size() - 1``),
     *  between 1 and the capacity; it starts at the capacity. An oscillator
     *  past the size keeps its state, frozen, and runs on from it when the
     *  size grows again.
     *
     *  ### Smoothing
     *
     *  A new frequency or magnitude is not jumped to: the value ramps there
     *  in a straight line over ``frequencySmoothing()`` resp.
     *  ``magnitudeSmoothing()`` samples (Max's ``freqsmooth`` / ``magsmooth``).
     *  0 or 1 means no smoothing: the new value is used from the sample it
     *  arrives on. A new value that arrives while a ramp runs starts a new
     *  ramp from where the old one got to; the same value again changes
     *  nothing. Smoothing counts samples, so it does not depend on the
     *  sample rate. ``clear`` and ``silence`` are immediate.
     *
     *  ### Setting oscillators
     *
     *  From code, ``set`` / ``setFrequency`` / ``setMagnitude`` /
     *  ``setPairs`` set one or more oscillators before the next block.
     *  ``operator()(frequency, magnitude, phase, index)`` sets them sample by
     *  sample from signals, Max's four inlets: at every sample the oscillator
     *  whose number is ``index`` (truncated toward zero; outside
     *  ``0..size()-1``, or not a finite number, nothing is set) is given the
     *  frequency and magnitude of that sample. This is how an analysis
     *  stream, one bin per sample, drives the bank.
     *
     *  With ``frameSync(true)`` the values sent sample by sample are held
     *  back: each oscillator keeps the last values sent to it, and all of
     *  them take effect together (begin their ramps) at the next sample
     *  whose index is 0, the start of the next analysis frame. In that mode
     *  the phase signal is used too: a phase sent to an oscillator (0..1,
     *  wrapped) is jumped to when its frame takes effect. Without frame sync
     *  the phase signal is ignored.
     *
     *  ### Table
     *
     *  Without interpolation the table is read at the point below the phase,
     *  so its size matters: ``tableSize`` (Max's ``tabpoints``) sets it to a
     *  power of two from 2 to 65536 (4096 by default). With interpolation it
     *  reads between two neighbouring points. The sine is one table of 65536
     *  points shared by every bank in the process, read at a stride, so
     *  changing the size allocates nothing and costs nothing.
     *
     *  Frequencies may be negative (the phase runs backwards) and above
     *  Nyquist (they alias, as in Max). A frequency, magnitude or phase that
     *  is not a finite number is taken as 0.
     *
     *  The phase increments are derived from the live sample rate at every
     *  block. Apart from the constructor and ``setTable``, call it from one
     *  thread at a time, the audio thread once it renders. Nothing else
     *  allocates: the output is sized to ``STANDARD_BUFFERSIZE`` in the
     *  constructor, and a block is at most that long.
     */
    class API oscillatorBank {
    public:
      /** @brief The capacity when none is given (Max's default). */
      static constexpr UInt DEFAULT_OSCILLATORS = 64;
      /** @brief The table size when none is given. */
      static constexpr UInt DEFAULT_TABLE_SIZE = 4096;
      /** @brief The smallest and the largest table. */
      static constexpr UInt MIN_TABLE_SIZE = 2;
      static constexpr UInt MAX_TABLE_SIZE = 65536;

      /**
       *  @brief A bank of @p capacity oscillators (at least 1), all at
       *         frequency 0 and magnitude 0. Allocates.
       *
       *  @p interpolating reads the table between points (``ioscbank~``).
       *  @p tableSize is rounded as ``tableSize()`` rounds it.
       */
      explicit oscillatorBank(UInt capacity = DEFAULT_OSCILLATORS, bool interpolating = false,
                              UInt tablePointCount = DEFAULT_TABLE_SIZE);

      /** @brief The most oscillators this bank can run. */
      UInt capacity() const {
        return static_cast<UInt>(oscillators.size());
      }

      /** @brief Run oscillators 0 to @p count - 1; 1 to the capacity. */
      oscillatorBank& size(UInt count);
      UInt size() const {
        return active;
      }

      /** @brief Ramp a new frequency over @p samples; 0 or 1: no ramp. */
      oscillatorBank& frequencySmoothing(UInt samples);
      UInt frequencySmoothing() const {
        return freqSmooth;
      }

      /** @brief Ramp a new magnitude over @p samples; 0 or 1: no ramp. */
      oscillatorBank& magnitudeSmoothing(UInt samples);
      UInt magnitudeSmoothing() const {
        return magSmooth;
      }

      /** @brief Hold values sent sample by sample until the next index 0. */
      oscillatorBank& frameSync(bool on);
      bool frameSync() const {
        return sync;
      }

      /** @brief Whether the table is read between points. */
      bool interpolating() const {
        return interpolate;
      }

      /**
       *  @brief Use a table of @p points points: rounded to the nearest power
       *         of two (the larger on a tie), from 2 to 65536.
       *
       *  Goes back to the sine: a waveform from ``setTable`` had the old
       *  size. Allocates nothing.
       */
      oscillatorBank& tableSize(UInt points);
      UInt tableSize() const {
        return tablePoints;
      }

      /**
       *  @brief Use @p count samples at @p samples as the table, in place of
       *         the sine.
       *
       *  The table keeps its size: the first ``tableSize()`` samples are
       *  used, missing ones are 0, values that are not finite numbers are 0.
       *  A null pointer or a count of 0 goes back to the sine. Allocates
       *  when the table is larger than any this bank had before: not from
       *  the audio thread while it renders.
       */
      oscillatorBank& setTable(const Flt* samples, UInt count);

      /** @brief Whether the table is one from ``setTable``. */
      bool customTable() const {
        return custom;
      }

      /** @brief Oscillator @p index ramps to @p frequency and @p magnitude.
       *         An index at or past the capacity is ignored. */
      oscillatorBank& set(UInt index, Flt frequency, Flt magnitude);
      oscillatorBank& setFrequency(UInt index, Flt frequency);
      oscillatorBank& setMagnitude(UInt index, Flt magnitude);

      /** @brief Jump oscillator @p index to @p phase (in cycles, wrapped). */
      oscillatorBank& setPhase(UInt index, Flt phase);

      /**
       *  @brief Max's ``set``: @p count frequency / magnitude pairs at
       *         @p pairs (2 * count values) go to oscillators 0 to
       *         count - 1, and every other oscillator ramps to magnitude 0.
       *
       *  Pairs past the capacity are ignored.
       */
      oscillatorBank& setPairs(const Flt* pairs, UInt count);

      /** @brief Every oscillator from @p first on ramps to magnitude 0. */
      oscillatorBank& fadeFrom(UInt first);

      /** @brief Every frequency and magnitude to 0, at once; held frame
       *         values are dropped. Phases stay. */
      oscillatorBank& clear();

      /** @brief Every magnitude to 0, at once. Frequencies stay. */
      oscillatorBank& silence();

      /** @brief ``clear``, and every phase back to 0. */
      oscillatorBank& reset();

      /** @brief Oscillator @p index's frequency, magnitude and phase as they
       *         are now (mid-ramp included); 0 past the capacity. */
      Flt frequency(UInt index) const;
      Flt magnitude(UInt index) const;
      Flt phase(UInt index) const;

      /** @brief One block of @p length samples, with the oscillators as
       *         they are set. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /**
       *  @brief One block, setting oscillators sample by sample: at sample
       *         ``i`` oscillator ``index[i]`` gets ``frequency[i]`` and
       *         ``magnitude[i]`` (and, with frame sync, ``phase[i]``).
       *
       *  The block is as long as the shortest of the four signals.
       */
      buffer& operator()(buffer& frequency, buffer& magnitude, buffer& phase, buffer& index);

      /** @brief The same without a phase signal: with frame sync, a frame
       *         sets frequencies and magnitudes and the phases run on. */
      buffer& operator()(buffer& frequency, buffer& magnitude, buffer& index);

    private:
      struct ramp {
        Flt value = 0.f;
        Flt target = 0.f;
        Flt step = 0.f;
        UInt left = 0;
      };

      struct oscillator {
        Dbl phase = 0.0;
        ramp freq;
        ramp mag;
        // Values held for the next frame (frame sync).
        Flt heldFreq = 0.f;
        Flt heldMag = 0.f;
        Flt heldPhase = 0.f;
        bool held = false;
        bool heldHasPhase = false;
      };

      static void Retarget(ramp& r, Flt target, UInt samples);
      void Write(UInt index, Flt frequency, Flt magnitude, const Flt* value);
      buffer& Process(buffer& frequency, buffer& magnitude, buffer* phase, buffer& index);
      void Commit();
      Flt Render(Dbl increment, const Flt* lookup);

      std::vector<oscillator> oscillators;
      std::vector<Flt> table; // setTable's waveform
      UInt stride = 1;
      UInt tablePoints = DEFAULT_TABLE_SIZE;
      UInt active = 1;
      UInt freqSmooth = 1;
      UInt magSmooth = 0;
      UInt heldCount = 0;
      bool interpolate = false;
      bool sync = false;
      bool custom = false;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_OSCILLATORBANK_HPP
