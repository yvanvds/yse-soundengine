/*
  ==============================================================================

    kink.hpp
    Phase distortion of a 0..1 ramp by a slope factor (issue #1060).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_KINK_HPP
#define YSE_DSP_GENERATORS_KINK_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Phase distortion: bends a 0..1 ramp (a ``phasor`` output) at
     *         the point where it reaches 0.5 sooner or later than the input.
     *
     *  Ported from Max's ``kink~``; the core of Casio-CZ-style phase
     *  distortion synthesis, when the bent phase reads a cosine (``cycle``
     *  with a phase input). For a phase ``x`` and a slope ``s``:
     *
     *  - while ``x * s`` is below 0.5, the output is ``x * s``;
     *  - from there on a second, complementary line runs from 0.5 at that
     *    input (the knee, ``x = 0.5 / s``) to 1 at ``x = 1``:
     *    ``0.5 + 0.5 * (x * s - 0.5) / (s - 0.5)``.
     *
     *  So the output still runs from 0 to 1 in the same time as the input,
     *  with a kink at the knee. A slope of 1 leaves the ramp unchanged; above
     *  1 the first half is passed faster, below 1 slower. At a slope of 0.5
     *  or less the knee lies at or past the end of the ramp, there is no
     *  second line, and the output is ``x * s`` throughout (it never reaches
     *  0.5 before the ramp wraps).
     *
     *  The input is not wrapped: values outside 0..1 follow the same two
     *  lines, extended. A phase that is not a finite number gives 0, a slope
     *  that is not one counts as 1, and a result that overflows is 0, so no
     *  NaN or infinity leaves this class.
     *
     *  The slope is a number or a second signal; with a signal the block is
     *  as long as the shorter input. A pure per-sample function: there is no
     *  state and nothing depends on the sample rate. Each call processes at
     *  most ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a
     *  time. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API kink {
    public:
      kink();

      /** @brief One sample: ``phase`` bent by ``slope``. */
      static Flt process(Flt phase, Flt slope);

      /** @brief One block against a constant ``slope``. */
      buffer& operator()(buffer& phase, Flt slope = 1.f);

      /** @brief One block against ``slope`` sample by sample. */
      buffer& operator()(buffer& phase, buffer& slope);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_KINK_HPP
