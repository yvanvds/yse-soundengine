/*
  ==============================================================================

    zeroCrossing.hpp
    Zero crossings of a signal: a count per block and a click on each
    (issue #1124).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_ZEROCROSSING_HPP
#define YSE_DSP_ANALYSIS_ZEROCROSSING_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Finds where a signal crosses zero: how many times per block,
     *         as a rough noisiness or pitch estimate, and a click on each
     *         crossing.
     *
     *  A crossing is a change of side: a sample above 0 after the last
     *  sample that was below it, or below 0 after the last one above it.
     *  Samples that are exactly 0 (or -0) or not a number are on neither
     *  side and keep the side of the sample before them, so ``1 0 -1`` is
     *  one crossing, at the ``-1``, and a signal that touches 0 and turns
     *  back is none. Until the first sample on a side there is no side, so
     *  the first non-zero sample is never a crossing. Infinities are on
     *  their side. The side at the end of a block is kept for the next one,
     *  so the crossings found do not depend on how the signal is split into
     *  blocks; ``reset()`` forgets it.
     *
     *  A block call gives two outputs the class owns: ``counts()``, the
     *  number of crossings in the block repeated over every sample of it,
     *  and ``clicks()``, the click volume on each sample that is a crossing
     *  and 0 elsewhere. It also reports the sample index of each crossing,
     *  in order, until the next block call. The click volume (default 1) can
     *  be any number; a negative one gives inverted clicks.
     *
     *  A block holds at most ``STANDARD_BUFFERSIZE`` samples; a longer input
     *  is processed up to that length. There is no sample-rate dependent
     *  state. Call it from one thread at a time. Processing allocates
     *  nothing: the outputs are sized in the constructor and the positions
     *  are a fixed array inside the object.
     */
    class API zeroCrossing {
    public:
      zeroCrossing();

      /** @brief One sample: true if @p in is a crossing. Remembers its side.
       *         Leaves the block outputs and report of the last block call
       *         unchanged. */
      bool process(Flt in);

      /** @brief One block: finds the crossings in @p in, fills ``counts()``
       *         and ``clicks()`` and replaces the report of the previous
       *         block call.
       *  @return The number of crossings in the block. */
      UInt operator()(buffer& in);

      /** @brief The crossing count of the last block call on every sample
       *         of that block. */
      buffer& counts();

      /** @brief The click volume on each crossing of the last block call,
       *         0 elsewhere. */
      buffer& clicks();

      /** @brief Crossings in the last block call. */
      UInt crossings() const;

      /** @brief The sample index of crossing @p i in the last block call, in
       *         order; @p i must be below ``crossings()``. */
      UInt crossing(UInt i) const;

      /** @brief Sets the click volume, used from the next block call. A
       *         value that is not a number is 0. */
      zeroCrossing& volume(Flt value);

      /** @brief The click volume. */
      Flt volume() const;

      /** @brief Forgets the side, as after construction: the next non-zero
       *         sample is not a crossing. Clears the block report; keeps the
       *         outputs and the click volume. */
      zeroCrossing& reset();

    private:
      Int side; // 1 above, -1 below, 0 none yet
      Flt clickVolume;
      UInt count;
      UInt pos[STANDARD_BUFFERSIZE];
      buffer countOut;
      buffer clickOut;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_ZEROCROSSING_HPP
