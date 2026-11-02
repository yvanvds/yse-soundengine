/*
  ==============================================================================

    minMax.hpp
    The running minimum and maximum of a signal (issue #1127).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_MINMAX_HPP
#define YSE_DSP_ANALYSIS_MINMAX_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The running minimum and maximum of a signal: the lowest and
     *         the highest sample value since the last reset.
     *
     *  Unlike ``peakTracker`` the values keep their sign and reading them
     *  does not start over: they run until ``reset()`` or a reset sample.
     *  NaN samples are skipped; an infinity counts like any other value.
     *  Until the first sample that is a number, both are 0.
     *
     *  A block call fills ``minimumSignal()`` and ``maximumSignal()``,
     *  buffers the object owns, with the minimum and the maximum after
     *  each sample. With a reset signal, every sample where it is not 0
     *  (and not NaN) starts over at that sample: the minimum and the
     *  maximum become that input sample. Where the reset signal is shorter
     *  than the input, it reads as 0.
     *
     *  A block holds at most ``STANDARD_BUFFERSIZE`` samples; a longer
     *  input is processed up to that length. The output buffers are made at
     *  that size in the constructor, so a block call never allocates. No
     *  state depends on the sample rate.
     *
     *  Call it from one thread at a time.
     */
    class API minMax {
    public:
      minMax();

      /** @brief One sample: tracks @p in. */
      void process(Flt in);

      /** @brief One sample with a reset: a @p reset that is not 0 (and
       *         not NaN) starts over at @p in. */
      void process(Flt in, Flt reset);

      /** @brief One block: tracks @p in and fills the output signals. */
      void operator()(buffer& in);

      /** @brief One block with a reset signal: tracks @p in, starting over
       *         at every sample where @p reset is not 0, and fills the
       *         output signals. */
      void operator()(buffer& in, buffer& reset);

      /** @brief The minimum after each sample of the last block call. */
      buffer& minimumSignal();

      /** @brief The maximum after each sample of the last block call. */
      buffer& maximumSignal();

      /** @brief The lowest sample value since the last reset; 0 before the
       *         first. */
      Flt minimum() const;

      /** @brief The highest sample value since the last reset; 0 before
       *         the first. */
      Flt maximum() const;

      /** @brief True while no sample has been tracked since the last
       *         reset. */
      bool empty() const;

      /** @brief Starts over: the next sample sets both the minimum and the
       *         maximum. Keeps the output signals of the last block call. */
      minMax& reset();

    private:
      void track(Flt in);
      void restart(Flt in);
      void fill(UInt length, const Flt* in, const Flt* reset, UInt resetLength);

      Flt low;
      Flt high;
      bool none;
      buffer lowOut;
      buffer highOut;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_MINMAX_HPP
