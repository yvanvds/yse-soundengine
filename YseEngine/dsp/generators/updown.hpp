/*
  ==============================================================================

    updown.hpp
    A trapezoid envelope with constant attack and release times, read by a
    ramp (issue #1070).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_UPDOWN_HPP
#define YSE_DSP_GENERATORS_UPDOWN_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A trapezoid envelope driven by a ramp (a ``phasor`` output) or
     *         a gate, whose attack and release take a fixed time however
     *         fast the ramp runs.
     *
     *  Ported from Max's ``updown~``. Unlike ``trapezoidShape``, whose ramps
     *  are fractions of the cycle and so speed up with the ramp, the rise
     *  takes ``up()`` milliseconds and the fall ``down()`` milliseconds:
     *
     *  - The **up** phase starts when the input goes from 0 to non-zero, or
     *    when the ramp resets (jumps by more than ``resetThreshold()``,
     *    0.5 by default, as a ``phasor`` does when it wraps).
     *  - The **down** phase ends where the ramp is expected to end: the
     *    slope of the input (its change per sample) predicts how many
     *    samples are left before a rising ramp reaches 1, or a falling one
     *    reaches 0, and the fall is timed to arrive at 0 there.
     *  - In between the output holds ``level()``. While the input is 0 the
     *    output falls to 0; a constant non-zero input (a gate) has no slope
     *    and holds ``level()``.
     *
     *  Per sample the envelope ``e`` (0..1) moves toward a target ``g`` by
     *  at most ``1 / upSamples`` upward and ``1 / downSamples`` downward;
     *  ``g`` is 0 while the input is 0 and otherwise
     *  ``min(1, remaining / downSamples)``, with ``remaining`` the samples
     *  left after this one before the ramp ends. The output is
     *  ``level * e``. For a 0..1 ramp of period ``P`` samples starting at 0
     *  that is the closed form
     *  ``min(n / upSamples, (P - 1 - n) / downSamples, 1)`` at sample ``n``
     *  of the cycle: 0 on its first and last samples. Because ``e`` only
     *  ever moves at these rates, a ramp that resets early, a gate that
     *  closes or a time that changes never makes the output jump: the
     *  envelope carries on from where it is (a new ``level()`` applies at
     *  once).
     *  ``restart()`` is the one exception: it drops the envelope to 0 so
     *  the next sample begins a fresh rise.
     *
     *  On a reset sample the jump is not taken as the slope; the previous
     *  slope is kept. A time of 0 (or less) makes that edge instant. Times
     *  are converted with the live ``SAMPLERATE`` at every block. An input
     *  that is not a finite number counts as 0; a setter given one is
     *  ignored.
     *
     *  Defaults: up and down 10 ms (Max documents no default), level 1,
     *  reset threshold 0.5. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API updown {
    public:
      static constexpr Flt DEFAULT_UP = 10.f;
      static constexpr Flt DEFAULT_DOWN = 10.f;
      static constexpr Flt DEFAULT_LEVEL = 1.f;
      static constexpr Flt DEFAULT_RESET = 0.5f;

      updown();

      /** @brief The rise time in milliseconds; below 0 counts as 0
       *         (instant). */
      updown& up(Flt ms);
      Flt up() const {
        return upTime;
      }

      /** @brief The fall time in milliseconds; below 0 counts as 0
       *         (instant). */
      updown& down(Flt ms);
      Flt down() const {
        return downTime;
      }

      /** @brief The output value at the top of the trapezoid. Any finite
       *         value; a negative level turns the envelope upside down. */
      updown& level(Flt value);
      Flt level() const {
        return top;
      }

      /** @brief The jump between two samples that counts as the ramp
       *         resetting; below 0 counts as 0. */
      updown& resetThreshold(Flt value);
      Flt resetThreshold() const {
        return threshold;
      }

      /** @brief Drop the envelope to 0: the next sample starts a new rise. */
      updown& restart();

      /** @brief The envelope's position, 0..1 (the output is this times
       *         ``level()``). */
      Flt envelope() const {
        return env;
      }

      /** @brief One block of the envelope for the ramp or gate ``input``. */
      buffer& operator()(buffer& input);

    private:
      buffer output;
      Flt upTime = DEFAULT_UP;
      Flt downTime = DEFAULT_DOWN;
      Flt top = DEFAULT_LEVEL;
      Flt threshold = DEFAULT_RESET;

      Flt env = 0.f;
      Flt previous = 0.f;
      Flt slope = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_UPDOWN_HPP
