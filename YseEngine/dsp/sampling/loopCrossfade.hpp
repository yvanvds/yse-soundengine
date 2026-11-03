/*
  ==============================================================================

    loopCrossfade.hpp
    Reading a looped region of a sample buffer with a crossfade across the
    loop point (issue #1140).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_LOOPCROSSFADE_HPP
#define YSE_DSP_SAMPLING_LOOPCROSSFADE_HPP

#include "../../headers/types.hpp"
#include "sampleBuffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A loop over frames ``low`` to ``high`` of a sample buffer,
     *         read with a crossfade across the loop point: Max's
     *         ``loopinterp`` with its ``interptime``.
     *
     *  A loop that jumps from its last frame back to its first clicks
     *  unless the two happen to meet. With a fade of ``fade`` frames, the
     *  last ``fade`` frames before the point the loop jumps from are mixed
     *  with the frames one loop length away, which are the ones that lead
     *  up to the point it jumps to. The mix moves in a straight line from
     *  all of the frame read to all of its partner, so the sound arriving
     *  at the jump is the sound the loop goes on with, and the jump is
     *  seamless.
     *
     *  - Playing **forward**, the loop jumps from ``high`` back to ``low``.
     *    A frame ``p`` in the last ``fade`` frames below ``high`` is mixed
     *    with ``p - length``: the frames just before ``low``.
     *  - Playing **backward**, the loop jumps from ``low`` up to ``high``.
     *    A frame ``p`` in the first ``fade`` frames above ``low`` is mixed
     *    with ``p + length``: the frames just after ``high``.
     *
     *  The partner frames are read from the buffer around the loop, so a
     *  loop that starts at the buffer's first frame fades its end toward
     *  the first sample (``sampleView::cubic`` holds positions inside the
     *  buffer). Reads go through ``sampleView::cubic``.
     *
     *  .. code-block:: cpp
     *
     *     DSP::sampleView view = reader.pin();
     *     const DSP::loopCrossfade xfade = DSP::loopCrossfade::make(lo, hi, 0.05 *
     * view.sampleRate()); Flt s = xfade.read(view, 0, position, true);
     *
     *  ``bufferPlayer`` (``~play``) loops through it; it is meant for every
     *  player with a loop crossfade (``~groove``). Two numbers and a fade,
     *  nothing allocated, any thread that holds the view.
     */
    struct loopCrossfade {
      /** @brief The loop's lower end, in frames. */
      Dbl low = 0.0;
      /** @brief The loop's upper end, in frames; above ``low``. */
      Dbl high = 0.0;
      /** @brief The crossfade, in frames: 0 (none) up to the loop length. */
      Dbl fade = 0.0;

      /** @brief The loop from @p lowFrame to @p highFrame with a crossfade
       *         of @p fadeFrames, cut to the loop's length; below 0, or not
       *         a number, is no crossfade. The ends are taken in either
       *         order. */
      static loopCrossfade make(Dbl lowFrame, Dbl highFrame, Dbl fadeFrames) {
        loopCrossfade x;
        x.low = lowFrame < highFrame ? lowFrame : highFrame;
        x.high = lowFrame < highFrame ? highFrame : lowFrame;
        const Dbl length = x.high - x.low;
        x.fade = !(fadeFrames > 0.0) ? 0.0 : (fadeFrames < length ? fadeFrames : length);
        return x;
      }

      /** @brief The loop's length in frames. */
      Dbl length() const {
        return high - low;
      }

      /**
       *  @brief How much of the partner frame goes into frame @p frame,
       *         playing forward or not: 0 outside the crossfade, rising to 1
       *         at the jump. @p partner is set to the partner frame when the
       *         weight is above 0.
       */
      Flt weight(Dbl frame, bool forward, Dbl& partner) const {
        if (!(fade > 0.0)) return 0.f;
        if (forward) {
          const Dbl from = high - fade;
          if (!(frame > from)) return 0.f;
          partner = frame - length();
          const Dbl g = (frame - from) / fade;
          return static_cast<Flt>(g < 1.0 ? g : 1.0);
        }
        const Dbl to = low + fade;
        if (!(frame < to)) return 0.f;
        partner = frame + length();
        const Dbl g = (to - frame) / fade;
        return static_cast<Flt>(g < 1.0 ? g : 1.0);
      }

      /** @brief Channel @p c at @p frame, crossfaded with its partner near
       *         the jump. */
      Flt read(const sampleView& view, UInt c, Dbl frame, bool forward) const {
        Dbl partner = 0.0;
        const Flt g = weight(frame, forward, partner);
        const Flt here = view.cubic(c, frame);
        if (g <= 0.f) return here;
        return here + g * (view.cubic(c, partner) - here);
      }
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_LOOPCROSSFADE_HPP
