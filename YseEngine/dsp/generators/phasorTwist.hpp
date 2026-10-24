/*
  ==============================================================================

    phasorTwist.hpp
    Bends a linear 0..1 phase ramp into an exponential curve (issue #1077).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORTWIST_HPP
#define YSE_DSP_GENERATORS_PHASORTWIST_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Bends a linear 0..1 ramp (a ``phasor`` output) into an
     *         exponential curve: accelerating or decelerating clocks and
     *         curved envelopes from a phasor.
     *
     *  Ported from Max's ``twist~``. The **curve** runs from -1 to 1; 0
     *  passes the ramp through unchanged. For input ``x`` and a curve
     *  ``c > 0`` the output is ``(e^(k x) - 1) / (e^k - 1)``: below the
     *  linear input, starting slow and speeding up. A curve ``c < 0`` gives
     *  the same shape turned half a turn, ``1 - (e^(k (1 - x)) - 1) /
     *  (e^k - 1)``: above the input, starting fast and slowing down. The
     *  steepness is ``k = h / (1 - h)`` with ``h = 0.91 (1.2 |c|)^0.41``,
     *  the exponential mapping of a ``curve~`` segment: about 0.6 at
     *  ``|c| = 0.1``, 2.8 at 0.5 and 50 at 1, so the bend grows sharply
     *  near the ends. Max describes its output as a piecewise linear
     *  approximation of this curve; here it is computed exactly, per
     *  sample. The curve is clamped to -1 .. 1; a value that is not a
     *  finite number is ignored. The output stays in 0 .. 1 and below 1,
     *  like the input: a value that rounds to 1 is held at the largest float
     *  below it, so an object reading it as a phase never sees a wrap.
     *
     *  **Falling ramps.** With ``shapeMode()`` on (the default) a falling
     *  ramp is bent the same way as a rising one: the curve is applied to
     *  how far the ramp has come, ``1 - x``, so a positive curve starts slow
     *  in either direction. With it off the same function of ``x`` is used
     *  either way, and a falling ramp traces the rising curve backwards. The
     *  direction is that of the last input step that moved; a held input
     *  keeps it, and the first ramp counts as rising. A new shape mode
     *  applies on the next sample.
     *
     *  **When a new curve lands.** With ``syncUpdate()`` off (the default) a
     *  new curve applies on the next sample, so the output can jump. With it
     *  on, it waits for the input ramp to start again: a wrap either way, or
     *  a change of direction. A curve set before the first sample applies
     *  from it, and turning ``syncUpdate()`` off lands a waiting curve on the
     *  next sample.
     *
     *  The input is wrapped into 0..1; a sample that is not a finite number
     *  counts as the previous one (0 before any). A fall of more than half a
     *  cycle is read as a forward wrap and a rise of more than half as a
     *  backward one. Nothing depends on the sample rate.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API phasorTwist {
    public:
      phasorTwist();

      /** @brief The curvature, -1 .. 1 (clamped); 0 leaves the ramp linear,
       *         above 0 bends it below the input, below 0 above it. A
       *         non-finite value is ignored. Lands as ``syncUpdate()`` says. */
      phasorTwist& curve(Flt value);
      /** @brief The last curve set, landed or not. */
      Flt curve() const {
        return requested;
      }

      /** @brief On (the default): falling ramps are bent like rising ones.
       *         Off: the same function of the input is used either way. */
      phasorTwist& shapeMode(bool on);
      bool shapeMode() const {
        return symmetric;
      }

      /** @brief On: a new curve waits for the input ramp to wrap or turn.
       *         Off (the default): it lands on the next sample. */
      phasorTwist& syncUpdate(bool on);
      bool syncUpdate() const {
        return sync;
      }

      /** @brief One sample for input ``phase``. */
      Flt process(Flt phase);

      /** @brief One block. */
      buffer& operator()(buffer& phase);

    private:
      Flt Step(Flt in);
      void Land();
      Dbl Bend(Dbl x) const;

      buffer output;
      Flt requested = 0.f;
      Flt active = 0.f;
      bool symmetric = true;
      bool sync = false;

      // The landed curve as steepness and e^k - 1; k = 0 is linear.
      Dbl steepness = 0.0;
      Dbl span = 0.0;

      // The (wrapped) input read last, whether there was one, and the
      // direction of the last step that moved (+1 rising, -1 falling).
      Flt previous = 0.f;
      bool started = false;
      int direction = 1;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORTWIST_HPP
