/*
  ==============================================================================

    trig.hpp
    Signal-rate trigonometric and hyperbolic functions (issue #1049).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_TRIG_HPP
#define YSE_DSP_OPERATORS_TRIG_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A trigonometric or hyperbolic function applied to every sample
     *         of a signal.
     *
     *  The function is chosen with ``setFunction`` (or the constructor).
     *  Angles are in radians. For a cosine driven by a 0..1 *phase* (Max's
     *  ``cos~``, turning a ``phasor`` into a sinusoid) use ``DSP::cosine``.
     *
     *  **No NaN or infinity leaves this class.** Out-of-domain input is
     *  clamped to the domain edge before the function runs:
     *
     *  - ``ACOS`` and ``ASIN`` clamp to -1..1, so ``acos(2)`` gives 0.
     *  - ``ACOSH`` clamps to 1 and above, so ``acosh(0)`` gives 0.
     *  - ``ATANH`` clamps to the largest float below 1 in magnitude, so
     *    ``atanh(1)`` gives about 8.66 rather than infinity.
     *  - ``COSH`` and ``SINH`` clamp their argument to -88..88, the widest
     *    range whose result still fits a float.
     *
     *  Whatever is still not finite after that (a NaN input, or the sine of
     *  an infinity) comes out as 0.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns. There is no sample-rate
     *  dependent state. Call it from one thread at a time. Processing
     *  allocates nothing: the output is sized to ``STANDARD_BUFFERSIZE`` in
     *  the constructor.
     */
    class API trig {
    public:
      /** @brief The function applied to each sample. */
      enum class FN {
        COS, ///< cosine of an angle in radians (Max ``cosx~``)
        SIN, ///< sine of an angle in radians (Max ``sinx~``)
        TAN, ///< tangent of an angle in radians (Max ``tanx~``)
        ACOS, ///< arc-cosine, input clamped to -1..1
        ASIN, ///< arc-sine, input clamped to -1..1
        ATAN, ///< arc-tangent
        COSH, ///< hyperbolic cosine, input clamped to -88..88
        SINH, ///< hyperbolic sine, input clamped to -88..88
        TANH, ///< hyperbolic tangent: a soft clipper to -1..1
        ACOSH, ///< hyperbolic arc-cosine, input clamped to 1 and above
        ASINH, ///< hyperbolic arc-sine
        ATANH, ///< hyperbolic arc-tangent, input clamped inside -1..1
      };

      /** @brief A processor applying ``fn``. */
      explicit trig(FN fn = FN::COS);

      /** @brief Choose the function. Takes effect on the next block. */
      trig& setFunction(FN fn);
      FN getFunction() const {
        return fn;
      }

      /** @brief One sample of ``fn``, clamped and made finite as above. */
      static Flt process(FN fn, Flt in);

      /** @brief One block: ``fn`` of every sample of ``in``. */
      buffer& operator()(buffer& in);

    private:
      FN fn;
      buffer output;
    };

    /**
     *  @brief Per-sample two-argument arc-tangent: the angle of the point
     *         (``x``, ``y``), in radians from -pi to pi.
     *
     *  ``y`` comes first, as in ``std::atan2`` and Max's ``atan2~``. ``x``
     *  is a number or a second signal; with a signal the block is as long as
     *  the shorter input. The origin gives 0. A NaN on either side gives 0,
     *  so no NaN leaves the class.
     *
     *  Block length, threading and allocation as ``trig``. There is no
     *  sample-rate dependent state.
     */
    class API arcTangent2 {
    public:
      arcTangent2();

      /** @brief One sample: the angle of (``x``, ``y``). */
      static Flt process(Flt y, Flt x);

      /** @brief One block against a constant ``x``. */
      buffer& operator()(buffer& y, Flt x);

      /** @brief One block against ``x`` sample by sample. */
      buffer& operator()(buffer& y, buffer& x);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_TRIG_HPP
