/*
  ==============================================================================

    frameClock.hpp
    Where frames of a sample stream begin and end, independent of the
    blocks the stream arrives in (issue #1160).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_FRAMECLOCK_HPP
#define YSE_DSP_SPECTRAL_FRAMECLOCK_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The schedule of a frame-based process over a sample stream:
     *         frames of ``size()`` samples, one starting every
     *         ``interval()`` samples, the first ``offset()`` samples into
     *         the stream.
     *
     *  Counting the stream's samples from 0, frame ``m`` (any whole
     *  number) covers samples ``offset + m * interval`` up to
     *  ``offset + m * interval + size - 1`` and is *complete* once its last
     *  sample has been taken in; whatever the frame produces starts at the
     *  next sample. The stream reads as 0 before its first sample, so the
     *  first frame to complete may have started before the stream did (with
     *  an offset, or an interval below the size): it completes on schedule,
     *  with zeros for the samples it missed.
     *
     *  The clock knows nothing about blocks. A block is processed as a few
     *  runs, each ending at the next event: ``untilFrame()`` is how many
     *  samples remain until a frame completes, ``advance(n)`` moves on by a
     *  run of at most that many and reports whether it completed one. A
     *  frame therefore completes at the same sample whatever the block size,
     *  and the work done for it runs at that sample, inside the block. This
     *  is how ``streamingFft`` and ``streamingInverseFft`` frame their
     *  input; an overlapping analysis (an interval below the size) uses the
     *  same clock.
     *
     *  ``position()`` is where the next sample falls in its period,
     *  ``(sample - offset) mod interval``: a process whose frames do not
     *  overlap (``interval >= size``) takes in the samples at positions
     *  ``0 .. size - 1`` and none after.
     *
     *  Header only, no allocation, nothing depends on the sample rate.
     */
    class frameClock {
    public:
      /** @brief Frames of @p size samples (at least 1) every @p interval
       *         samples (at least 1), the first @p offset samples in (taken
       *         modulo the interval). */
      frameClock(UInt size = 512, UInt interval = 512, UInt offset = 0)
        : frameSize(size < 1 ? 1 : size), period(interval < 1 ? 1 : interval) {
        phase = offset % period;
        reset();
      }

      /** @brief Back to the start of the stream: sample 0 comes next. */
      void reset() {
        pos = (period - phase) % period;
      }

      /** @brief A new offset, taken modulo the interval. The stream keeps
       *         its place: the next sample moves to its position under the
       *         new offset. */
      void offset(UInt samples) {
        const UInt next = samples % period;
        // position = (sample - offset) mod interval
        pos = static_cast<UInt>((static_cast<U64>(pos) + phase + period - next) % period);
        phase = next;
      }

      UInt size() const {
        return frameSize;
      }
      UInt interval() const {
        return period;
      }
      UInt offset() const {
        return phase;
      }

      /** @brief Where the next sample falls in its period, ``0`` to
       *         ``interval() - 1``. */
      UInt position() const {
        return pos;
      }

      /** @brief Samples until the next frame completes, counting the one
       *         that completes it: 1 to ``interval()``. */
      UInt untilFrame() const {
        // A frame completes when the position reaches size mod interval.
        const UInt end = frameSize % period;
        const UInt n = (end + period - pos) % period;
        return n == 0 ? period : n;
      }

      /** @brief Moves on by @p samples, at most ``untilFrame()``. True when
       *         those samples completed a frame. */
      bool advance(UInt samples) {
        const bool completes = samples >= untilFrame();
        pos = static_cast<UInt>((static_cast<U64>(pos) + samples) % period);
        return completes;
      }

    private:
      UInt frameSize;
      UInt period;
      UInt phase = 0;
      UInt pos = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_FRAMECLOCK_HPP
