/*
  ==============================================================================

    edgeDetector.hpp
    Zero / non-zero transitions of a signal (issue #1121).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_EDGEDETECTOR_HPP
#define YSE_DSP_ANALYSIS_EDGEDETECTOR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Finds the logical transitions of a signal: where it goes from
     *         zero to non-zero (a rise) and from non-zero to zero (a fall).
     *
     *  A sample is zero when it compares equal to 0, so -0 is zero, and any
     *  other value is non-zero: the smallest denormal, an infinity and a
     *  sample that is not a number all count as non-zero. Turns a gate, a
     *  comparator output (``~>``, ``~==`` …) or a pulse train into the
     *  positions where it opens and closes.
     *
     *  The sample before the first is zero, so a signal that starts non-zero
     *  rises on its first sample. The state at the end of a block is kept
     *  for the next one, so the transitions found do not depend on how the
     *  signal is split into blocks. ``set()`` chooses the remembered state,
     *  ``reset()`` sets it back to zero.
     *
     *  A block call reports every transition in the block: how many rises
     *  and falls there were and the sample index of each, in order, until
     *  the next block call. A block holds at most ``STANDARD_BUFFERSIZE``
     *  samples; a longer input is processed up to that length. There is no
     *  sample-rate dependent state.
     *
     *  Call it from one thread at a time. Nothing allocates: the position
     *  lists are fixed arrays inside the object.
     */
    class API edgeDetector {
    public:
      edgeDetector();

      /** @brief One sample: 1 if @p in rises (zero before, non-zero now), -1
       *         if it falls (non-zero before, zero now), 0 otherwise.
       *         Remembers the state of @p in. Leaves the block report of the
       *         last block call unchanged. */
      Int process(Flt in);

      /** @brief One block: finds the rises and falls in @p in and replaces
       *         the report of the previous block call with them.
       *  @return The number of transitions, rises and falls together. */
      UInt operator()(buffer& in);

      /** @brief Rises in the last block call. */
      UInt rises() const;

      /** @brief Falls in the last block call. */
      UInt falls() const;

      /** @brief The sample index of rise @p i in the last block call, in
       *         order; @p i must be below ``rises()``. */
      UInt rise(UInt i) const;

      /** @brief The sample index of fall @p i in the last block call, in
       *         order; @p i must be below ``falls()``. */
      UInt fall(UInt i) const;

      /** @brief Whether the remembered sample is non-zero: the state of the
       *         last input, or of the last ``set()`` / ``reset()`` since. */
      bool high() const;

      /** @brief Sets the remembered state: the next sample rises only if
       *         @p nonZero is false, and falls only if it is true. Clears
       *         the block report. */
      edgeDetector& set(bool nonZero);

      /** @brief Sets the remembered state to zero, as after construction,
       *         and clears the block report. */
      edgeDetector& reset();

    private:
      bool state;
      UInt riseCount;
      UInt fallCount;
      UInt risePos[STANDARD_BUFFERSIZE];
      UInt fallPos[STANDARD_BUFFERSIZE];
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_EDGEDETECTOR_HPP
