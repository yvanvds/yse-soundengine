/*
  ==============================================================================

    direction.hpp
    The direction a signal moves in, sample by sample (issue #1120).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_DIRECTION_HPP
#define YSE_DSP_ANALYSIS_DIRECTION_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The direction of a signal: each output sample is 1 when the
     *         input sample is above the one before it, -1 when it is below,
     *         and 0 when it is the same.
     *
     *  The sign of the first difference, y[n] = sign(x[n] - x[n-1]). A rising
     *  ramp gives a steady 1, a falling one -1, a constant 0; on a sine the
     *  output flips between 1 and -1 at every peak and trough. On a
     *  ``phasor`` it is 1 except at each wrap, where one sample is -1.
     *
     *  The sample before the first is 0, so the first output is the sign of
     *  the first input. The last input of a block is kept for the next one,
     *  so the result does not depend on how the signal is split into blocks.
     *  ``reset()`` sets the remembered sample back to 0, ``set()`` to any
     *  value.
     *
     *  The comparison is exact, in float: any rise, however small, gives 1.
     *  A sample that is not a number compares neither above nor below, so it
     *  gives 0 for its own output and for the next one; the output after that
     *  is normal again. Infinities compare as the largest and smallest
     *  values.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns; a longer input is processed up
     *  to that length. There is no sample-rate dependent state.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API direction {
    public:
      direction();

      /** @brief One sample: 1 if @p in is above the previous input, -1 if
       *         it is below, 0 otherwise; remembers @p in. */
      Flt process(Flt in);

      /** @brief One block: the direction from each sample of @p in to the
       *         next. */
      buffer& operator()(buffer& in);

      /** @brief Sets the remembered sample to @p value: the next output
       *         compares that sample's input with ``value``. */
      direction& set(Flt value);

      /** @brief Sets the remembered sample to 0, as after construction. */
      direction& reset();

      /** @brief The remembered sample: the last input, or the value of the
       *         last ``set()`` / ``reset()`` since. */
      Flt previous() const;

    private:
      Flt last;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_DIRECTION_HPP
