/*
  ==============================================================================

    delta.hpp
    The difference between successive samples of a signal (issue #1108).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_DELTA_HPP
#define YSE_DSP_OPERATORS_DELTA_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A first difference: each output sample is the input sample
     *         minus the input sample before it, y[n] = x[n] - x[n-1].
     *
     *  Differentiates a signal: a ramp becomes its slope, a step becomes a
     *  one-sample impulse and an impulse a doublet. On a ``phasor`` the
     *  output is the small, steady rise per sample except where the phase
     *  wraps, which shows as one large negative sample: a simple way to find
     *  the start of each cycle.
     *
     *  The sample before the first is 0, so the first output is the first
     *  input. The last input of a block is kept for the next one, so the
     *  result does not depend on how the signal is split into blocks.
     *  ``reset()`` sets the remembered sample back to 0, ``set()`` to any
     *  value.
     *
     *  The difference is taken in float, as is. A sample that is not a
     *  finite number makes its own output and the next one non-finite; the
     *  output after that is normal again.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns; a longer input is processed up
     *  to that length. There is no sample-rate dependent state.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API delta {
    public:
      delta();

      /** @brief One sample: returns @p in minus the previous input, and
       *         remembers @p in. */
      Flt process(Flt in);

      /** @brief One block: the difference between each sample of @p in and
       *         the one before it. */
      buffer& operator()(buffer& in);

      /** @brief Sets the remembered sample to @p value: the next output is
       *         that sample's input minus ``value``. */
      delta& set(Flt value);

      /** @brief Sets the remembered sample to 0, as after construction. */
      delta& reset();

      /** @brief The remembered sample: the last input, or the value of the
       *         last ``set()`` / ``reset()`` since. */
      Flt previous() const;

    private:
      Flt last;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_DELTA_HPP
