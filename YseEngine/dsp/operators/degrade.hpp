/*
  ==============================================================================

    degrade.hpp
    A bitcrusher: reduces the effective sample rate and the bit depth of a
    signal (issue #1106).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_DEGRADE_HPP
#define YSE_DSP_OPERATORS_DEGRADE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A bitcrusher: holds the input at a fraction of the sample rate
     *         and quantizes it to a number of bits.
     *
     *  Ported from MSP's ``degrade~``. Two settings:
     *
     *  - The *ratio*, 0 to 1, is the fraction of the sample rate at which a
     *    new input sample is taken; between two takes the last one is held.
     *    A phase rises by the ratio every sample and a sample is taken each
     *    time it reaches 1, starting with the first sample after
     *    construction or ``reset()``. At a constant ratio ``r`` the k-th
     *    take (from 0) is at sample ``ceil(k / r)``: 1 takes every sample,
     *    0.5 every second one, 0.25 every fourth one. A fractional period
     *    keeps its fraction in double precision, so it does not drift. 0
     *    holds the last sample taken for good. The ratio counts samples, not
     *    seconds, so the class has no sample-rate state: it follows the live
     *    rate by construction.
     *  - The *bits*, 1 to 24, set the quantization. With ``b`` bits and
     *    ``q = 2^(b-1)`` the output is ``floor(held * q) / q``: the range -1
     *    to 1 is cut into ``2^b`` steps of ``1/q``, each sample rounded down
     *    to one of them, so 1 bit gives -1 or 0 and 24 bits steps of
     *    ``2^-23``. The quantization is applied to every output sample with
     *    the bits of that sample. Nothing is clipped: a sample beyond -1..1
     *    is quantized with the same step.
     *
     *  The defaults, ratio 1 and 24 bits, take every sample and quantize
     *  with steps of ``2^-23`` only. A ratio outside 0..1 is clamped to it,
     *  bits outside 1..24 likewise (a fractional number of bits read from a
     *  signal is truncated); NaN settings are ignored. An input sample that
     *  is not a finite number is not taken: the held sample stays, so a
     *  stray NaN cannot latch the output.
     *
     *  The three-buffer call reads the ratio and the bits per sample; the
     *  getters keep the last values read. ``reset()`` sets the held sample
     *  to 0 and makes the next sample a take; it keeps the settings.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API degrade {
    public:
      /** @brief The ratio until it is set: every sample is taken. */
      static constexpr Flt DEFAULT_RATIO = 1.f;
      /** @brief The bits until they are set. */
      static constexpr int DEFAULT_BITS = 24;
      /** @brief The fewest bits. */
      static constexpr int MIN_BITS = 1;
      /** @brief The most bits. */
      static constexpr int MAX_BITS = 24;

      degrade();

      /** @brief The fraction of the sample rate at which the input is
       *         taken, clamped to 0..1. NaN: ignored. */
      degrade& ratio(Flt value);

      /** @brief The ratio as last set or read. */
      Flt ratio() const {
        return rate;
      }

      /** @brief The number of bits, clamped to 1..24. */
      degrade& bits(int value);

      /** @brief The bits as last set or read. */
      int bits() const {
        return depth;
      }

      /** @brief Sets the held sample to 0 and makes the next sample a take,
       *         as after construction. Keeps the settings. */
      degrade& reset();

      /** @brief The sample last taken, before quantization. */
      Flt held() const {
        return hold;
      }

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with the ratio and the bits read per sample from
       *         @p ratioSignal and @p bitsSignal; as long as the shortest.
       *         They also set the getters. */
      buffer& operator()(buffer& in, buffer& ratioSignal, buffer& bitsSignal);

    private:
      Flt Step(Flt in);
      void Depth(int value);

      buffer output;

      double phase = 1.0;
      Flt hold = 0.f;
      Flt rate = DEFAULT_RATIO;
      int depth = DEFAULT_BITS;
      Flt scale = 8388608.f; // 2^(DEFAULT_BITS - 1)
      Flt step = 1.f / 8388608.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_DEGRADE_HPP
