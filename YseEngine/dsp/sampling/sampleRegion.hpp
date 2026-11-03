/*
  ==============================================================================

    sampleRegion.hpp
    A span of a sample buffer given in milliseconds, in frames (issue #1143).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SAMPLEREGION_HPP
#define YSE_DSP_SAMPLING_SAMPLEREGION_HPP

#include <cmath>
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The span of a sample buffer an object records or loops over,
     *         in frames: from ``start`` up to (not including) ``end``.
     *
     *  The sampling objects take their loop points in milliseconds, with an
     *  end of 0 meaning the end of the buffer. ``fromMs`` turns such a pair
     *  into frames for one block, at the rate the buffer's samples are at
     *  and for the length it has then, so the span follows a buffer that is
     *  reloaded, resized or given another rate. Make one per block from the
     *  view you pinned:
     *
     *  .. code-block:: cpp
     *
     *     DSP::sampleWriteView view = writer.pin();
     *     const DSP::sampleRegion span =
     *         DSP::sampleRegion::fromMs(startMs, endMs, view.sampleRate(), view.frames());
     *     Flt ramp = span.phase(position);   // 0 at start, 1 at end
     *
     *  ``bufferRecorder`` uses it for its record loop and its sync output; a
     *  player that loops a region and reports where it is uses it the same
     *  way. It holds two numbers and allocates nothing.
     */
    struct sampleRegion {
      /** @brief The first frame of the span. */
      UInt start = 0;
      /** @brief The frame after the last one: ``start`` plus the length. */
      UInt end = 0;

      /** @brief Whether the span holds no frame. */
      bool empty() const {
        return end <= start;
      }

      /** @brief How many frames the span holds. */
      UInt length() const {
        return empty() ? 0 : end - start;
      }

      /** @brief Whether @p frame lies in the span. */
      bool contains(Dbl frame) const {
        return frame >= static_cast<Dbl>(start) && frame < static_cast<Dbl>(end);
      }

      /**
       *  @brief Where @p frame lies in the span: 0 at ``start``, rising in a
       *         straight line to 1 at ``end``. What a sync output sends.
       *
       *  Outside the span it goes on in the same line (below 0, or 1 and
       *  above); an empty span is 0 everywhere.
       */
      Flt phase(Dbl frame) const {
        if (empty()) return 0.f;
        return static_cast<Flt>((frame - static_cast<Dbl>(start)) / static_cast<Dbl>(end - start));
      }

      /**
       *  @brief The span from @p startMs to @p endMs of a buffer of
       *         @p frames frames whose samples are at @p sampleRate Hz.
       *
       *  Each point is rounded to the nearest frame, as ``sampleBuffer``
       *  rounds a length in milliseconds. A start below 0, or one that is
       *  not a number, is 0; past the buffer it is the buffer's end. An end
       *  of 0 or below, one that is not a number, or one past the buffer is
       *  the buffer's end. An end at or before the start gives an empty
       *  span.
       */
      static sampleRegion fromMs(Flt startMs, Flt endMs, Flt sampleRate, UInt frames) {
        sampleRegion span;
        span.start = MsToFrame(startMs, sampleRate, frames);
        span.end = endMs > 0.f ? MsToFrame(endMs, sampleRate, frames) : frames;
        return span;
      }

      /**
       *  @brief The span from frame @p startFrame to @p endFrame of a buffer
       *         of @p frames frames, with the same rules as ``fromMs``: an
       *         end of 0 is the buffer's end, and both are kept inside it.
       */
      static sampleRegion fromFrames(UInt startFrame, UInt endFrame, UInt frames) {
        sampleRegion span;
        span.start = startFrame < frames ? startFrame : frames;
        span.end = endFrame > 0 && endFrame < frames ? endFrame : frames;
        return span;
      }

    private:
      static UInt MsToFrame(Flt ms, Flt sampleRate, UInt frames) {
        if (!(ms > 0.f) || !(sampleRate > 0.f)) return 0;
        const Dbl at =
            std::floor(static_cast<Dbl>(ms) * static_cast<Dbl>(sampleRate) * 0.001 + 0.5);
        return at >= static_cast<Dbl>(frames) ? frames : static_cast<UInt>(at);
      }
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEREGION_HPP
