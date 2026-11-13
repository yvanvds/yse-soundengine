/*
  ==============================================================================

    lineSegments.hpp
    A list of straight ramp segments played back to back (issue #1042).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_LINESEGMENTS_HPP
#define YSE_DSP_GENERATORS_LINESEGMENTS_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A linear ramp generator that plays a list of segments back to
     *         back, each a target and a time in milliseconds: envelopes,
     *         fades and glides.
     *
     *  Ported from Max's ``line~``; the class behind ``~line``.
     *
     *  **Segments.** ``segments()`` takes up to ``capacity()`` segments.
     *  The first starts from the current value, wherever a running list had
     *  got to, and replaces that list; each later one starts from the
     *  previous target. A segment lasting ``N`` samples (its time turned
     *  into samples at the live ``SAMPLERATE``, read on every sample) is at
     *  ``from + (to - from) * k / N`` on its ``k``-th sample, so it reaches
     *  its target on its ``N``-th. ``N`` need not be whole: the part of a
     *  sample left over at the end of a segment is carried into the next
     *  one, so a list does not drift. A time of 0 (or less, or not a
     *  number) is a jump that takes no time, so ``0 0 1 500`` jumps to 0 and
     *  rises to 1 over half a second. After the last segment the output
     *  holds the last target.
     *
     *  ``set(target, time)`` is a list of one segment. With a time of 0 it
     *  jumps at once, before the next block, so a list given right after it
     *  starts from the new value; the next block call still reports
     *  ``finished()``. ``jump()`` sets the value the same way without
     *  counting as a finished list: use it for an initial value.
     *
     *  **Transport.** ``stop()`` holds the current value and drops the list.
     *  ``pause()`` holds it but keeps the place in the list, and ``resume()``
     *  carries on from there; a new list, a ``set()`` or a jump ends a
     *  pause. A block call reports ``finished()`` when the list ran out in
     *  it (or a ``set()`` jumped before it), which a stop does not count as.
     *
     *  **Activity.** ``activity()`` is the last block's busy state, sample
     *  by sample: 1 while a list is still heading for a target, 0 from the
     *  sample that reaches its last target on, while paused, and for a
     *  jump. It is Max's ``activeout`` signal.
     *
     *  Defaults: value 0, no list. Apart from the constructor, call it from
     *  one thread at a time, the audio thread once it renders. Nothing but
     *  the constructor allocates: the list is sized to the capacity and the
     *  outputs to ``STANDARD_BUFFERSIZE``; a block is at most that long (a
     *  longer one is processed up to that length).
     */
    class API lineSegments {
    public:
      /** @brief The capacity of a default-constructed generator. */
      static constexpr UInt DEFAULT_POINTS = 128;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_POINTS = 4096;

      /** @brief A generator with room for @p capacity segments (1 to
       *         ``MAX_POINTS``). Allocates. */
      explicit lineSegments(UInt capacity = DEFAULT_POINTS);

      /** @brief How many segments a list can hold. */
      UInt capacity() const {
        return static_cast<UInt>(targets.size());
      }

      /**
       *  @brief Plays @p count segments of @p targets and @p times (ms) from
       *         the current value.
       *
       *  Negative or non-finite times count as 0. A list longer than the
       *  capacity is cut to it. Returns how many segments were taken; 0,
       *  changing nothing, when @p count is 0 or a target is not a finite
       *  number.
       */
      UInt segments(const Flt* targets, const Flt* times, UInt count);

      /** @brief One segment to @p target over @p time ms; a time of 0 (or
       *         less, or not a number) jumps at once and reports
       *         ``finished()`` in the next block call. A target that is not
       *         a finite number is ignored. */
      lineSegments& set(Flt target, Flt time);

      /** @brief Sets the value at once and drops the list, without counting
       *         as a finished list. A value that is not a finite number is
       *         ignored. */
      lineSegments& jump(Flt value);

      /** @brief Holds the current value and drops the list. */
      lineSegments& stop();

      /** @brief Holds the current value, keeping the place in the list. */
      lineSegments& pause();

      /** @brief Carries on after ``pause()``. */
      lineSegments& resume();

      /** @brief The value of the last sample (or of the last jump). */
      Flt value() const {
        return current;
      }

      /** @brief True while a list plays (not paused, not finished). */
      bool active() const {
        return remaining() > 0 && !held;
      }

      /** @brief True while a list is paused. */
      bool paused() const {
        return held && remaining() > 0;
      }

      /** @brief Segments not yet finished, the running one included. */
      UInt remaining() const {
        return count - index;
      }

      /** @brief True when the list ran out in the last block call. */
      bool finished() const {
        return ended;
      }

      /** @brief One block of the ramp. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief The last block's busy state: 1 or 0 per sample, as long as
       *         the last block. */
      buffer& activity() {
        return busy;
      }

      /** @brief Back to the state after construction: value 0, no list, not
       *         paused. */
      lineSegments& reset();

    private:
      Flt Step();
      void Drop();

      std::vector<Flt> targets;
      std::vector<Flt> times;
      UInt count = 0;
      UInt index = 0;
      bool held = false;
      bool ended = false;
      bool jumped = false; // a set() jumped: the next block reports finished
      Flt current = 0.f;
      Flt from = 0.f; // the running segment's start
      Dbl elapsed = 0.0; // samples into the running segment
      buffer output;
      buffer busy;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_LINESEGMENTS_HPP
