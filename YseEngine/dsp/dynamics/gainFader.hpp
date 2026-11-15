/*
  ==============================================================================

    gainFader.hpp
    A mixer fader: scales a signal by a slider position on a log taper,
    ramping to each new gain over an interpolation time (issue #1168).

  ==============================================================================
*/

#ifndef YSE_DSP_DYNAMICS_GAINFADER_HPP
#define YSE_DSP_DYNAMICS_GAINFADER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../filters/rampSmooth.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A mixer fader: multiplies a signal by the gain of a slider
     *         position, read off a log taper, and ramps to each new gain
     *         over the interpolation time so a move does not click.
     *
     *  Ported from Max's ``gain~``. The slider has ``size()`` steps, the
     *  positions 0 to ``size() - 1``. Position 0 is silence; any other
     *  position @c p has the gain
     *
     *  @code
     *  gain = scale * increment ^ (p - size)
     *  @endcode
     *
     *  the formula of Max's ``linedrive``. Each step multiplies the gain by
     *  the increment. With the defaults (158 steps, scale 7.94321, increment
     *  1.071519) a step is 0.6 dB and ten steps 6 dB, position 128 is unity
     *  gain (the scale is the increment to the 30th power), position 1 is
     *  75.6 dB below 127, and the top position, 157, is 17.4 dB above unity.
     *
     *  **Ramp.** When the gain changes, the gain applied to the signal moves
     *  to it in a straight line over the interpolation time, ``time()`` in
     *  milliseconds (default 10), by ``DSP::rampSmooth``. The time becomes a
     *  number of samples at the live ``SAMPLERATE``, read on every call. A
     *  move during a ramp starts a new one from where the gain is; a time
     *  below one sample jumps. A new fader starts at silence, so the first
     *  position set fades in.
     *
     *  **Settings.** A position or size is used as its whole part (Max's
     *  are ints) and a position is held to the slider, so it moves when the
     *  size shrinks below it; the size is at least 2 (silence and one
     *  gain) and at most ``MAX_SIZE``. A setting that is not a number is
     *  ignored, as is a scale that is not finite or an increment that is not
     *  a finite number above 0. A negative scale inverts the signal. A
     *  negative time is 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API gainFader {
    public:
      /** @brief The number of steps until it is set. */
      static constexpr Int DEFAULT_SIZE = 158;
      /** @brief The gain one step below the top of the slider is
       *         ``scale / increment``; this is the scale until it is set,
       *         which makes position 128 of 158 unity gain. */
      static constexpr Flt DEFAULT_SCALE = 7.94321f;
      /** @brief The factor between neighbouring positions until it is set:
       *         0.6 dB. */
      static constexpr Flt DEFAULT_INCREMENT = 1.071519f;
      /** @brief The interpolation time in milliseconds until it is set. */
      static constexpr Flt DEFAULT_TIME = 10.f;
      /** @brief The most steps a slider can have. */
      static constexpr Int MAX_SIZE = 1 << 20;

      /** @brief The gain of @p position on a slider of @p size steps with
       *         @p scale and @p increment: 0 at position 0 (or below),
       *         ``scale * increment ^ (position - size)`` otherwise. */
      static Flt Gain(Int position, Int size, Flt scale, Flt increment);

      /** @brief @p position as the fader uses it: its whole part, held to
       *         0 .. @p size - 1. NaN is 0. */
      static Int Position(Flt position, Int size);

      /** @brief @p steps as the fader uses it as a size: its whole part,
       *         held to 2 .. ``MAX_SIZE``. NaN is ``DEFAULT_SIZE``. */
      static Int Size(Flt steps);

      gainFader();

      /** @brief The slider position. NaN: ignored. */
      gainFader& position(Flt value);

      /** @brief The number of steps; the position is held to the new
       *         slider. NaN: ignored. */
      gainFader& size(Flt steps);

      /** @brief The gain one step below the top of the slider times the
       *         increment. Not finite: ignored. */
      gainFader& scale(Flt value);

      /** @brief The factor between neighbouring positions. Not a finite
       *         number above 0: ignored. */
      gainFader& increment(Flt value);

      /** @brief The interpolation time in milliseconds; negative is 0.
       *         NaN: ignored. */
      gainFader& time(Flt ms);

      /** @brief The position in use. */
      Int position() const {
        return slider;
      }

      /** @brief The number of steps in use. */
      Int size() const {
        return steps;
      }

      /** @brief The scale in use. */
      Flt scale() const {
        return base;
      }

      /** @brief The increment in use. */
      Flt increment() const {
        return factor;
      }

      /** @brief The interpolation time in use, in milliseconds. */
      Flt time() const {
        return interpolation;
      }

      /** @brief The gain of the position: where the ramp heads. */
      Flt gain() const {
        return Gain(slider, steps, base, factor);
      }

      /** @brief The gain applied to the last sample processed. */
      Flt value() const {
        return smoother.value();
      }

      /** @brief One block: @p in times the ramped gain. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
      rampSmooth smoother;

      Int slider = 0;
      Int steps = DEFAULT_SIZE;
      Flt base = DEFAULT_SCALE;
      Flt factor = DEFAULT_INCREMENT;
      Flt interpolation = DEFAULT_TIME;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DYNAMICS_GAINFADER_HPP
