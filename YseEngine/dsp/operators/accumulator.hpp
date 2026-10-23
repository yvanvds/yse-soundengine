/*
  ==============================================================================

    accumulator.hpp
    A running sum of a signal, sample by sample (issue #1047).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_ACCUMULATOR_HPP
#define YSE_DSP_OPERATORS_ACCUMULATOR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A signal accumulator: each output sample is the sum of every
     *         input sample so far.
     *
     *  Integrates a signal: a constant input becomes a ramp, a stream of 1s
     *  counts samples, and a frequency divided by the sample rate becomes a
     *  phase. An input of 1, 1, 1, 1 from a sum of 0 gives 1, 2, 3, 4: the
     *  current sample is added before it is output.
     *
     *  The sum is kept in double precision, so a long accumulation of small
     *  values does not drift. Each output sample is that sum rounded to a
     *  float. Nothing wraps or limits the sum; it can grow very large.
     *
     *  The sum can be reset in three ways: ``reset()`` sets it to 0,
     *  ``set()`` sets it to any value, and a reset signal passed to
     *  ``operator()`` sets it to 0 at every sample where the reset signal is
     *  not 0. At such a sample the sum restarts from that sample's input, so
     *  the output there is the input itself.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns; a longer input is processed up
     *  to that length. With a reset signal the block is as long as the
     *  shorter of the two inputs. There is no sample-rate dependent state.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API accumulator {
    public:
      /** @brief Starts with a sum of @p initial. */
      explicit accumulator(Dbl initial = 0.0);

      /** @brief One sample: adds @p in to the sum and returns the new sum. */
      Flt process(Flt in);

      /**
       *  @brief One sample with a reset: when @p reset is not 0 the sum is
       *         set to 0 first, so the result is @p in.
       */
      Flt process(Flt in, Flt reset);

      /** @brief One block: the running sum of @p in. */
      buffer& operator()(buffer& in);

      /**
       *  @brief One block with a per-sample reset: the sum is set to 0 before
       *         each sample where @p reset is not 0.
       */
      buffer& operator()(buffer& in, buffer& reset);

      /** @brief Sets the sum to @p value. The next sample output is
       *         ``value`` plus that sample's input. */
      accumulator& set(Dbl value);

      /** @brief Sets the sum to 0. */
      accumulator& reset();

      /** @brief The current sum: the last sample output, or the value of the
       *         last ``set()`` / ``reset()`` since. */
      Dbl sum() const;

    private:
      Dbl total;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_ACCUMULATOR_HPP
