/*
  ==============================================================================

    phaseShapes.hpp
    Triangle and trapezoid shapes of a 0..1 phase (issue #1063).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASESHAPES_HPP
#define YSE_DSP_GENERATORS_PHASESHAPES_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A triangle with a movable peak, read by a 0..1 phase (a
     *         ``phasor`` output): LFO shapes and envelopes.
     *
     *  Ported from Max's ``triangle~``. For a phase ``x`` and a peak
     *  position ``p``, the shape rises in a straight line from ``lo`` at
     *  ``x = 0`` to ``hi`` at ``x = p``, then falls in a straight line back
     *  to ``lo`` at ``x = 1``:
     *
     *  - ``x < p``: ``x / p``, scaled into ``lo..hi``;
     *  - otherwise: ``(1 - x) / (1 - p)``, scaled into ``lo..hi``.
     *
     *  A peak of 0.5 is a symmetric triangle; 0 is a falling ramp (it starts
     *  at ``hi``), 1 a rising sawtooth. The peak is held within 0..1; one
     *  that is not a finite number counts as 0.5.
     *
     *  The phase is wrapped into 0..1 (its fractional part, so -0.4 reads as
     *  0.6), as a wavetable is; a phase that is not a finite number counts as
     *  0. The output range is ``lo`` to ``hi``, -1 to 1 by default; ``lo``
     *  may be above ``hi``, which turns the shape upside down. The scaling is
     *  ``lo * (1 - u) + hi * u``, so any finite range gives a finite result.
     *
     *  The peak is a number or a second signal; with a signal the block is
     *  as long as the shorter input. A pure function of its inputs and the
     *  range: nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API triangleShape {
    public:
      triangleShape();

      /** @brief One sample: the shape at ``phase`` with its peak at ``peak``,
       *         scaled into ``lo..hi``. A range that is not finite gives 0. */
      static Flt process(Flt phase, Flt peak, Flt lo = -1.f, Flt hi = 1.f);

      /** @brief The output range. A value that is not a finite number is
       *         ignored and the old one kept. */
      triangleShape& range(Flt lo, Flt hi);
      Flt lo() const {
        return low;
      }
      Flt hi() const {
        return high;
      }

      /** @brief One block against a constant ``peak``. */
      buffer& operator()(buffer& phase, Flt peak = 0.5f);

      /** @brief One block against ``peak`` sample by sample. */
      buffer& operator()(buffer& phase, buffer& peak);

    private:
      buffer output;
      Flt low = -1.f;
      Flt high = 1.f;
    };

    /**
     *  @brief A trapezoid read by a 0..1 phase (a ``phasor`` output): a rise,
     *         a plateau and a fall, for LFO shapes and envelopes.
     *
     *  Ported from Max's ``trapezoid~``. For a phase ``x``, a ramp-up point
     *  ``a`` and a ramp-down point ``d``, the shape rises in a straight line
     *  from ``lo`` at ``x = 0`` to ``hi`` at ``x = a``, holds ``hi`` until
     *  ``x = d``, then falls in a straight line back to ``lo`` at ``x = 1``:
     *
     *  - ``x < a``: ``x / a``;
     *  - ``a <= x < d``: 1;
     *  - otherwise: ``(1 - x) / (1 - d)`` (1 when ``d`` is 1),
     *
     *  scaled into ``lo..hi``. The defaults 0.1 and 0.9 give a trapezoid;
     *  0.5 and 0.5 a triangle; 0 and 0 a falling ramp. Both points are held
     *  within 0..1, and a ramp-down point before the ramp-up point is moved
     *  to it (no plateau). A point that is not a finite number counts as its
     *  default.
     *
     *  **Wrap mode.** With wrapping on (the default, Max's ``wrapmode 1``)
     *  the phase is its fractional part, so 4.3 reads as 0.3 and -0.4 as
     *  0.6. With wrapping off (``wrapmode 0``) it is clipped to 0..1. A phase
     *  that is not a finite number counts as 0.
     *
     *  The output range is ``lo`` to ``hi``, 0 to 1 by default; ``lo`` may be
     *  above ``hi``. The scaling is ``lo * (1 - u) + hi * u``, so any finite
     *  range gives a finite result.
     *
     *  The points are numbers or signals; with signals the block is as long
     *  as the shortest input. Nothing depends on the sample rate. Each call
     *  processes at most ``STANDARD_BUFFERSIZE`` samples. Call it from one
     *  thread at a time. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API trapezoidShape {
    public:
      static constexpr Flt DEFAULT_UP = 0.1f;
      static constexpr Flt DEFAULT_DOWN = 0.9f;

      trapezoidShape();

      /** @brief One sample: the shape at ``phase``, scaled into ``lo..hi``,
       *         with the phase wrapped (``wrap``) or clipped. A range that is
       *         not finite gives 0. */
      static Flt process(Flt phase, Flt up, Flt down, Flt lo = 0.f, Flt hi = 1.f, bool wrap = true);

      /** @brief The output range. A value that is not a finite number is
       *         ignored and the old one kept. */
      trapezoidShape& range(Flt lo, Flt hi);
      Flt lo() const {
        return low;
      }
      Flt hi() const {
        return high;
      }

      /** @brief Wrap the phase into 0..1 (true, the default) or clip it. */
      trapezoidShape& wrap(bool on);
      bool wrap() const {
        return wrapping;
      }

      /** @brief One block against constant ramp points. */
      buffer& operator()(buffer& phase, Flt up = DEFAULT_UP, Flt down = DEFAULT_DOWN);

      /** @brief One block against ramp points sample by sample. */
      buffer& operator()(buffer& phase, buffer& up, buffer& down);

    private:
      buffer output;
      Flt low = 0.f;
      Flt high = 1.f;
      bool wrapping = true;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASESHAPES_HPP
