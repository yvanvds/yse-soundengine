/*
  ==============================================================================

    polar.hpp
    Signal-rate Cartesian / polar conversion and phase wrapping (issue #1053).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_POLAR_HPP
#define YSE_DSP_OPERATORS_POLAR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Per-sample Cartesian to polar conversion: a real / imaginary
     *         pair (``re``, ``im``) to magnitude and phase.
     *
     *  The magnitude is ``hypot(re, im)``, the phase ``atan2(im, re)`` in
     *  radians from -pi to pi, so the origin gives magnitude 0 and phase 0.
     *  Ported from Max's ``cartopol~``; useful on any quadrature pair, such
     *  as the two outputs of ``DSP::hilbert`` (giving its envelope and
     *  instantaneous phase).
     *
     *  ``im`` is a number or a second signal; with a signal the block is as
     *  long as the shorter input. Each call fills ``magnitude()`` and
     *  ``phase()``, which the class owns and keeps until the next call.
     *
     *  **No NaN or infinity leaves this class:** a non-finite result is 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. There is
     *  no sample-rate dependent state. Call it from one thread at a time.
     *  Processing allocates nothing: both outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API cartToPolar {
    public:
      cartToPolar();

      /** @brief One sample: the magnitude and phase of (``re``, ``im``). */
      static void process(Flt re, Flt im, Flt& magnitude, Flt& phase);

      /** @brief One block against a constant ``im``. */
      void operator()(buffer& re, Flt im);

      /** @brief One block against ``im`` sample by sample. */
      void operator()(buffer& re, buffer& im);

      /** @brief The magnitude of the last block, 0 or higher. */
      buffer& magnitude() {
        return magnitudeOut;
      }

      /** @brief The phase of the last block, -pi to pi. */
      buffer& phase() {
        return phaseOut;
      }

    private:
      buffer magnitudeOut;
      buffer phaseOut;
    };

    /**
     *  @brief Per-sample polar to Cartesian conversion: magnitude and phase
     *         (radians) to a real / imaginary pair.
     *
     *  ``re = magnitude * cos(phase)``, ``im = magnitude * sin(phase)``: the
     *  inverse of ``cartToPolar``. Ported from Max's ``poltocar~``.
     *
     *  ``phase`` is a number or a second signal; with a signal the block is
     *  as long as the shorter input. Each call fills ``real()`` and
     *  ``imaginary()``. A non-finite result is 0. Block length, threading and
     *  allocation as ``cartToPolar``; no sample-rate dependent state.
     */
    class API polarToCart {
    public:
      polarToCart();

      /** @brief One sample: the real and imaginary parts of the point. */
      static void process(Flt magnitude, Flt phase, Flt& re, Flt& im);

      /** @brief One block against a constant ``phase``. */
      void operator()(buffer& magnitude, Flt phase);

      /** @brief One block against ``phase`` sample by sample. */
      void operator()(buffer& magnitude, buffer& phase);

      /** @brief The real part of the last block. */
      buffer& real() {
        return realOut;
      }

      /** @brief The imaginary part of the last block. */
      buffer& imaginary() {
        return imaginaryOut;
      }

    private:
      buffer realOut;
      buffer imaginaryOut;
    };

    /**
     *  @brief Wraps every sample of a signal into -pi..pi.
     *
     *  Adds or removes whole turns (2 pi) until the angle lies in -pi to pi:
     *  ``3 pi / 2`` gives ``-pi / 2``, and ``pi`` itself gives ``-pi``.
     *  Ported from Max's ``phasewrap~``; use it on a phase difference or an
     *  accumulated phase before comparing angles. The wrap is computed in
     *  double precision, so large angles keep their fractional turn as well
     *  as a float allows. A non-finite input gives 0.
     *
     *  Block length, threading and allocation as ``cartToPolar``; no
     *  sample-rate dependent state.
     */
    class API phaseWrap {
    public:
      phaseWrap();

      /** @brief One sample, wrapped into -pi..pi. */
      static Flt process(Flt in);

      /** @brief One block: every sample of ``in`` wrapped. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_POLAR_HPP
