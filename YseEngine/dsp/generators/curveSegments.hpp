/*
  ==============================================================================

    curveSegments.hpp
    A list of curved ramp segments played back to back (issue #1065).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_CURVESEGMENTS_HPP
#define YSE_DSP_GENERATORS_CURVESEGMENTS_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A ramp generator that plays a list of segments back to back,
     *         each with a target, a time in milliseconds and a curvature:
     *         envelopes, fades and glides that bend.
     *
     *  Ported from Max's ``curve~``.
     *
     *  **Segments.** ``segments()`` takes up to ``capacity()`` segments.
     *  The first starts from the current value, wherever a running list had
     *  got to, and replaces that list; each later one starts from the
     *  previous target. A segment lasting ``N`` samples (its time turned
     *  into samples at the live ``SAMPLERATE``, read on every sample) is at
     *  ``from + (to - from) * shape(k / N)`` on its ``k``-th sample, so it
     *  reaches its target on its ``N``-th. ``N`` need not be whole: the part
     *  of a sample left over at the end of a segment is carried into the
     *  next one, so a list does not drift. A time of 0 (or less, or not a
     *  number) is a jump that takes no time. After the last segment the
     *  output holds the last target. ``jump()`` sets the value at once and
     *  drops the list.
     *
     *  **Curvature** (-1..1) bends a segment: 0 is a straight line, a
     *  positive curve leaves slowly and arrives quickly (exponential, for a
     *  rise that sounds even), a negative one the opposite (logarithmic);
     *  +1 holds the start until the end and -1 jumps at the start. Two
     *  shapes are available, ``c`` and ``-c`` mirroring each other in both:
     *
     *  - ``POWER`` (the default): ``shape(f) = f^((1 + c) / (1 - c))``, the
     *    curve ``.function``, ``~shape`` and ``~ramp`` draw, so a segment
     *    copied from a ``.function`` plays as it is drawn. ``-c`` is the
     *    inverse of ``c``, its reflection across the diagonal.
     *  - ``EXPONENTIAL``: ``shape(f) = (e^(k f) - 1) / (e^k - 1)`` with
     *    ``k = 2 ln((1 + c) / (1 - c))``, a true exponential, gentler at
     *    the ends than the power curve. ``-c`` is ``c`` turned half round
     *    about the middle of the segment.
     *
     *  Max documents the range of ``curve~``'s parameter but not its
     *  formula, so these are YSE's. A curve that is not a number is 0.
     *
     *  **Transport.** ``stop()`` holds the current value and drops the list.
     *  ``pause()`` holds it but keeps the place in the list, and ``resume()``
     *  carries on from there; a new list or a jump ends a pause. A block
     *  call reports ``finished()`` when the list ran out in it, which a
     *  stop does not count as.
     *
     *  Defaults: value 0, no list, power shape. Apart from the constructor,
     *  call it from one thread at a time, the audio thread once it renders.
     *  Nothing but the constructor allocates: the list is sized to the
     *  capacity and the output to ``STANDARD_BUFFERSIZE``; a block is at
     *  most that long (a longer one is processed up to that length).
     */
    class API curveSegments {
    public:
      /** @brief The curve formulas; see the class description. */
      enum SHAPE : int {
        POWER = 0,
        EXPONENTIAL = 1,
      };

      /** @brief The capacity of a default-constructed generator. */
      static constexpr UInt DEFAULT_POINTS = 128;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_POINTS = 4096;

      /** @brief A generator with room for @p capacity segments (1 to
       *         ``MAX_POINTS``). Allocates. */
      explicit curveSegments(UInt capacity = DEFAULT_POINTS);

      /** @brief How many segments a list can hold. */
      UInt capacity() const {
        return static_cast<UInt>(targets.size());
      }

      /**
       *  @brief Plays @p count segments of @p targets, @p times (ms) and
       *         @p curves (-1..1; nullptr for all 0) from the current value.
       *
       *  Negative or non-finite times count as 0; curves are clamped to
       *  -1..1. A list longer than the capacity is cut to it. Returns how
       *  many segments were taken; 0, changing nothing, when @p count is 0
       *  or a target is not a finite number.
       */
      UInt segments(const Flt* targets, const Flt* times, const Flt* curves, UInt count);

      /** @brief Sets the value at once and drops the list. A value that is
       *         not a finite number is ignored. */
      curveSegments& jump(Flt value);

      /** @brief Holds the current value and drops the list. */
      curveSegments& stop();

      /** @brief Holds the current value, keeping the place in the list. */
      curveSegments& pause();

      /** @brief Carries on after ``pause()``. */
      curveSegments& resume();

      /** @brief Sets the curve formula. Default ``POWER``. */
      curveSegments& shape(SHAPE mode);
      SHAPE shape() const {
        return mode;
      }

      /** @brief The value of the last sample. */
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

      /** @brief Back to the state after construction: value 0, no list, not
       *         paused. Keeps the shape. */
      curveSegments& reset();

    private:
      Flt Step();
      void Begin(); // prepares segment `index`, starting at `current`
      Flt Shape(Flt f) const;

      std::vector<Flt> targets;
      std::vector<Flt> times;
      std::vector<Flt> curves;
      UInt count = 0;
      UInt index = 0;
      SHAPE mode = POWER;
      bool held = false;
      bool ended = false;
      Flt current = 0.f;
      Flt from = 0.f; // the running segment's start
      Flt bend = 1.f; // its exponent (POWER) or k (EXPONENTIAL)
      Flt scale = 1.f; // EXPONENTIAL: 1 / (e^k - 1)
      Dbl elapsed = 0.0; // samples into the running segment
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_CURVESEGMENTS_HPP
