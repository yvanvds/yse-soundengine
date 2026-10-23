/*
  ==============================================================================

    rounding.hpp
    Signal-rate rounding and truncation (issue #1050).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_ROUNDING_HPP
#define YSE_DSP_OPERATORS_ROUNDING_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Rounds every sample of a signal to a multiple of a step.
     *
     *  A step of 1 rounds to whole numbers, 0.5 to halves, 12 to octaves of
     *  semitones. The sign of the step is ignored. **A step of 0 passes the
     *  signal through unchanged**, so a patch can switch rounding off
     *  without rewiring (as Max's ``round~`` and YSE's ``.round`` do).
     *
     *  Two modes:
     *
     *  - *nearest* (the default): the nearest multiple; halfway values
     *    round away from zero, so 0.5 gives 1 and -0.5 gives -1.
     *  - *floor* (``setNearest(false)``): the multiple at or below the
     *    sample, so 0.9 gives 0 and -0.1 gives -1.
     *
     *  The step is a number (``setStep``) or a second signal, sample by
     *  sample. **No NaN or infinity leaves this class:** a step so small
     *  that the quotient leaves the float range passes the sample through,
     *  and a non-finite input sample gives 0.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns. There is no sample-rate
     *  dependent state. Call it from one thread at a time; the setters
     *  are atomic and may be called from any thread. Processing allocates
     *  nothing: the output is sized to ``STANDARD_BUFFERSIZE`` in the
     *  constructor.
     */
    class API round {
    public:
      /** @brief A rounder with step ``stepValue`` in nearest (``true``) or floor mode. */
      explicit round(Flt stepValue = 1.f, bool nearestValue = true);

      /** @brief The step used by ``operator()(buffer&)``. 0 turns rounding off. */
      round& setStep(Flt value);
      Flt getStep() const {
        return step;
      }

      /** @brief ``true`` rounds to the nearest multiple, ``false`` rounds down. */
      round& setNearest(bool value);
      bool getNearest() const {
        return nearest;
      }

      /** @brief One sample: ``in`` rounded to a multiple of ``step``. */
      static Flt process(Flt in, Flt step, bool nearest = true);

      /** @brief One block against the step set with ``setStep``. */
      buffer& operator()(buffer& in);

      /** @brief One block against ``step`` sample by sample. The block is as
       *         long as the shorter input. */
      buffer& operator()(buffer& in, buffer& step);

    private:
      aFlt step;
      aBool nearest;
      buffer output;
    };

    /**
     *  @brief Drops the fractional part of every sample, rounding toward
     *         zero: 1.7 gives 1, -1.7 gives -1.
     *
     *  A non-finite input sample gives 0. Block length, threading and
     *  allocation as ``round``. There is no sample-rate dependent state.
     */
    class API trunc {
    public:
      trunc();

      /** @brief One sample, truncated toward zero. */
      static Flt process(Flt in);

      /** @brief One block. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_ROUNDING_HPP
