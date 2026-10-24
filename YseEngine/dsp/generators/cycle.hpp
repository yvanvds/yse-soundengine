/*
  ==============================================================================

    cycle.hpp
    A wavetable oscillator with a phase input (issue #1054).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_CYCLE_HPP
#define YSE_DSP_GENERATORS_CYCLE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief A wavetable oscillator: reads one cycle of a waveform over and
     *         over at a given frequency, with linear interpolation, and adds a
     *         phase input to the running phase for phase modulation.
     *
     *  The default waveform is a cosine, read from a built-in table of
     *  ``TABLE_SIZE`` points, so at phase 0 the output is 1. ``table`` swaps
     *  in any other single-cycle waveform, for instance the first channel of
     *  a ``sampleBuffer``.
     *
     *  Each sample is the table at ``frac(running + phase)``: ``running``
     *  advances by ``frequency / SAMPLERATE`` per sample (kept in double
     *  precision, so a slow oscillator does not drift), and ``phase`` is the
     *  phase input, in cycles (0..1 is one cycle; any value is wrapped). A
     *  phase input that moves at audio rate is phase modulation, the same
     *  sound as FM. A negative frequency runs the table backwards.
     *
     *  The sample rate is read from ``SAMPLERATE`` on every block, so the
     *  frequency stays right across an engine restart at another rate.
     *
     *  Apart from the constructor and the phase accessors, call it only from
     *  the audio thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     *  With two signal inputs the block is as long as the shorter one. A
     *  frequency or phase that is not a finite number restarts the phase at
     *  0 rather than letting a NaN through.
     */
    class API cycle {
    public:
      /** @brief Points in the built-in cosine table. */
      static constexpr UInt TABLE_SIZE = 16384;

      /** @brief How many samples of a ``sampleView`` a table uses when no
       *         size is given. */
      static constexpr UInt DEFAULT_VIEW_SIZE = 512;

      cycle();

      /** @brief One block at a constant frequency (Hz) and phase offset
       *         (cycles). */
      buffer& operator()(Flt frequency, Flt phase = 0.f, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency, at a constant
       *         phase offset. */
      buffer& operator()(buffer& frequency, Flt phase = 0.f);

      /** @brief One block at a constant frequency, driven by a per-sample
       *         phase. */
      buffer& operator()(Flt frequency, buffer& phase);

      /** @brief One block driven by a per-sample frequency and phase. */
      buffer& operator()(buffer& frequency, buffer& phase);

      /**
       *  @brief Read @p size samples at @p samples as the waveform from the
       *         next block on.
       *
       *  The table is not copied: it must stay valid while blocks are made
       *  from it. Interpolation wraps from the last point back to the first.
       *  A null pointer or a size of 0 is an empty table, which outputs
       *  silence. ``cosine`` goes back to the built-in table.
       */
      cycle& table(const Flt* samples, UInt size);

      /**
       *  @brief Use the first channel of a pinned buffer as the waveform.
       *
       *  @param view    The buffer's samples for this block. Call this every
       *                 block, after ``sampleBufferReader::pin``, and make the
       *                 block before the view goes: the table points into it.
       *  @param offset  The first sample of the table.
       *  @param size    How many samples from @p offset. 0 means
       *                 ``DEFAULT_VIEW_SIZE``, a negative number everything up
       *                 to the end of the buffer. It is cut short at the end
       *                 of the buffer.
       *
       *  An empty view, or an offset past the end, is an empty table: the
       *  output is silence until the buffer has samples.
       */
      cycle& table(const sampleView& view, UInt offset = 0, Int size = 0);

      /** @brief Go back to the built-in cosine table. */
      cycle& cosine();

      /** @brief The number of points in the table in use. */
      UInt tableSize() const {
        return size;
      }

      /**
       *  @brief Set the running phase now. The value is wrapped into 0..1.
       *         The next sample is the table at this phase plus the phase
       *         input.
       */
      cycle& reset(Flt phase = 0.f);

      /** @brief The running phase the next sample starts from, in 0..1,
       *         without the phase input. */
      Flt phase() const;

    private:
      template <class Frequency, class Phase>
      buffer& process(Frequency frequency, Phase phase, UInt length);

      Flt lookup(Dbl phase) const;

      const Flt* data;
      UInt size;
      Dbl current;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_CYCLE_HPP
