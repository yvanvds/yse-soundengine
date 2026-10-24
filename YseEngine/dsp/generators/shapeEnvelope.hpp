/*
  ==============================================================================

    shapeEnvelope.hpp
    A breakpoint envelope played over the length of a ramp (issue #1071).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_SHAPEENVELOPE_HPP
#define YSE_DSP_GENERATORS_SHAPEENVELOPE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A breakpoint envelope whose length follows the ramp that plays
     *         it: one ``phasor`` cycle (or one ``ramp``) is one pass through
     *         the envelope, however long it takes.
     *
     *  Ported from Max's ``shape~``.
     *
     *  **The envelope** is an initial value followed by up to ``capacity()``
     *  breakpoints, each a target value, a time in milliseconds to reach it
     *  from the previous value, and a curvature (``line~``'s format, as the
     *  second outlet of a ``function`` produces it). A time of 0 is a jump.
     *  The times are only proportions: in the scaled part of the envelope
     *  they are divided by their total, so the whole envelope fits the ramp.
     *  With no breakpoints the output is the initial value.
     *
     *  **The ramp.** The input is read as a position through the envelope:
     *  a rising ramp (0 to 1) as it is, a falling one (1 to 0) as ``1 - x``,
     *  clamped to 0..1 (a sample that is not a number counts as 0). The
     *  ramp *starts* when the input leaves 0 or jumps by more than 0.5 between
     *  two samples (a ``phasor`` wrapping). The direction is taken from the
     *  input's slope: a start that jumps up (a falling ramp beginning near 1)
     *  is falling, any other start rising, and between starts the sign of
     *  each change decides (no change keeps the direction). At a
     *  position ``p`` the envelope is at time ``p * total``; within the
     *  segment there, ``f`` (0..1) of the way through it, the output is
     *  ``from + (to - from) * shape(f)``. Past the end it holds the last
     *  value.
     *
     *  **Curvature** (-1..1) shapes the segment arriving at its breakpoint:
     *  ``shape(f) = f^((1 + c) / (1 - c))``, the patcher's ``.function``
     *  curve. 0 is linear, a positive curve leaves slowly and arrives
     *  quickly, a negative one the opposite; +1 holds until the end and -1
     *  jumps at the start. Max documents the range but not the formula, so
     *  this one is YSE's. With ``curved(false)`` (Max's ``curvemode`` off,
     *  the default) every segment is played linearly, whatever its curve.
     *
     *  **Constant segments.** ``constant(n)`` plays the first ``n`` segments
     *  (an attack) at their own times in milliseconds, counted in samples at
     *  the live ``SAMPLERATE`` from the start of the ramp, however long the
     *  ramp is. When they end, at input position ``x0``, the remaining
     *  segments are scaled into the rest of the ramp, ``x0`` to 1. Only a
     *  rising ramp plays constant segments; a falling one scales the whole
     *  envelope. While the input stays at 0 (the ramp is off) the clock is
     *  held at the start of the envelope.
     *
     *  **Updates.** A new envelope (``breakpoints()``) takes over at once,
     *  or, with ``syncUpdate(true)``, at the next start of the ramp, so a
     *  pass in progress finishes with the envelope it began with. Of several
     *  envelopes set before a start, the last one wins. Turning syncUpdate
     *  off applies a waiting envelope at once.
     *
     *  Defaults: initial value 0 and no breakpoints (the output is 0, as in
     *  Max until a list arrives), no constant segments, curves off,
     *  syncUpdate off.
     *
     *  Apart from the constructor, call it from one thread at a time, the
     *  audio thread once it renders. Nothing but the constructor allocates:
     *  both envelope stores are sized to the capacity and the output to
     *  ``STANDARD_BUFFERSIZE``; a block is at most that long (a longer one
     *  is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API shapeEnvelope {
    public:
      /** @brief The capacity of a default-constructed envelope. */
      static constexpr UInt DEFAULT_POINTS = 256;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_POINTS = 4096;
      /** @brief The jump between two samples that counts as a ramp
       *         starting again. */
      static constexpr Flt RESET_THRESHOLD = 0.5f;

      /** @brief An envelope with room for @p capacity breakpoints (1 to
       *         ``MAX_POINTS``). Allocates. */
      explicit shapeEnvelope(UInt capacity = DEFAULT_POINTS);

      /** @brief How many breakpoints the envelope has room for. */
      UInt capacity() const {
        return static_cast<UInt>(current.values.size());
      }

      /**
       *  @brief Set the envelope: @p initial, then @p count breakpoints of
       *         @p values, @p times (ms) and @p curves (-1..1; nullptr for
       *         all 0).
       *
       *  Negative times count as 0; curves are clamped to -1..1. Changes
       *  nothing and returns false when @p count exceeds the capacity or a
       *  value is not a finite number. Applied at once, or at the next start
       *  with ``syncUpdate(true)``.
       */
      bool breakpoints(Flt initial, const Flt* values, const Flt* times, const Flt* curves,
                       UInt count);

      /** @brief The playing envelope's breakpoint count. */
      UInt count() const {
        return current.count;
      }
      /** @brief The playing envelope's initial value. */
      Flt initial() const {
        return current.initial;
      }
      /** @brief Breakpoint @p point's value, time and curve (0 past the
       *         count). */
      Flt value(UInt point) const;
      Flt time(UInt point) const;
      Flt curve(UInt point) const;

      /** @brief True while an envelope waits for the next start
       *         (syncUpdate). */
      bool pending() const {
        return waiting;
      }

      /** @brief How many leading segments play at their own times rather
       *         than scaled to the ramp. */
      shapeEnvelope& constant(UInt segments);
      UInt constant() const {
        return constantSegments;
      }

      /** @brief Whether segments play with their curvature (Max's
       *         ``curvemode``). */
      shapeEnvelope& curved(bool on);
      bool curved() const {
        return useCurves;
      }

      /** @brief Whether a new envelope waits for the next start of the
       *         ramp. */
      shapeEnvelope& syncUpdate(bool on);
      bool syncUpdate() const {
        return sync;
      }

      /** @brief One block of the envelope for the ramp @p input. */
      buffer& operator()(buffer& input);

    private:
      struct store {
        std::vector<Flt> values;
        std::vector<Flt> times;
        std::vector<Flt> curves;
        std::vector<Flt> exponents;
        std::vector<Flt> ends; // cumulative times
        UInt count = 0;
        Flt initial = 0.f;
      };

      void Commit();
      // The envelope at time `ms` along it.
      Flt At(Flt ms);

      store current;
      store next;
      bool waiting = false;
      UInt constantSegments = 0;
      bool useCurves = false;
      bool sync = false;
      buffer output;

      UInt segment = 0; // the cached segment index
      Flt previous = 0.f; // the last input sample
      bool rising = true; // the ramp's direction
      bool justStarted = false;
      UInt elapsed = 0; // samples since the start
      bool scaling = false; // the constant segments are over
      Flt scaleFrom = 0.f; // the position where they ended
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_SHAPEENVELOPE_HPP
