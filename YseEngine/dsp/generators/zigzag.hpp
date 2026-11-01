/*
  ==============================================================================

    zigzag.hpp
    An editable breakpoint function played at variable speed (issue #1067).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_ZIGZAG_HPP
#define YSE_DSP_GENERATORS_ZIGZAG_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A list of points, each a value and a time in milliseconds,
     *         played as straight ramps forward or backward at any speed,
     *         looped, and edited while it plays.
     *
     *  Ported from Max's ``zigzag~``.
     *
     *  **Points.** Point ``i`` has a value and a time (its *delta*): the
     *  time a forward ramp takes from point ``i - 1`` to point ``i``. Point
     *  0's delta is the time from wherever the output is to the first point
     *  when the list is played. ``setLine()`` replaces the list; ``append()``,
     *  ``insert()``, ``remove()`` and ``set()`` edit it, also while it plays:
     *  the running ramp keeps heading for the same point, wherever an edit
     *  moved it, and reads that point's value live. Negative or non-finite
     *  times count as 0. Up to ``capacity()`` points.
     *
     *  **Playing.** A ramp runs from the current output to a target point.
     *  Forward, when it gets there the next one starts towards the next
     *  point over that point's delta; backward, towards the previous point
     *  over the delta of the point just reached. The time runs at
     *  ``|speed|`` (1 is as written, 0.5 half as fast, 0 holds); a negative
     *  speed plays backward. Reversing in the middle of a ramp turns back to
     *  the point it came from, at the same rate. A ramp lasting ``N``
     *  samples (the time turned into samples at the live ``SAMPLERATE``,
     *  read on every sample) reaches its point on its ``N``-th sample, and
     *  what is left of a sample carries into the next ramp, so a list does
     *  not drift. The output is a straight line: Max's ``zigzag~`` has no
     *  curves.
     *
     *  **Range and loops.** ``start()`` and ``end()`` (or ``bound()``) limit
     *  play to part of the list; by default all of it. When play reaches the
     *  end of the range (the start, playing backward) it stops with
     *  ``LOOP_OFF``, starts again from the other end with ``LOOP_ON`` (over
     *  the delta of the range's first point), or turns round with
     *  ``LOOP_PENDULUM``. Each of these counts in ``ends()``. A range of one
     *  point does not loop.
     *
     *  **Moving.** ``jump()`` ramps to a point over a time and plays on from
     *  there; ``jumpStart()``, ``jumpEnd()``, ``next()``, ``prev()`` and
     *  ``skip()`` are jumps to the range's ends or relative to the current
     *  index. ``play()`` starts the list from the start of the range (the
     *  end, playing backward). ``bang()`` depends on the mode: ``START`` and
     *  ``TRIGGER`` jump to the start of the range (the end, backward) over
     *  the ``bangDelta()`` time, ``STEP`` jumps to the next point (the
     *  previous one, backward). ``stop()`` holds the output where it is.
     *
     *  **Input signal.** The block call can take a signal. In ``START`` mode
     *  it is an index into the list: the output is the list read at that
     *  index, with straight lines between points (1.5 is halfway between
     *  points 1 and 2), and play, speed and range are ignored. In
     *  ``TRIGGER`` and ``STEP`` modes, each sample where the signal starts to
     *  fall (its slope turns from 0 or rising to falling, as a ``~phasor``
     *  wraps) is a ``bang()``. The speed can also be a signal, read once per
     *  block from its first sample, as Max does.
     *
     *  **Index.** ``indices()`` holds the index of each sample: the point
     *  the running ramp is heading for, the point last reached once play
     *  stops, or the input index in ``START`` mode with a signal.
     *
     *  Defaults: output 0, no points, speed 1, ``LOOP_OFF``, ``START``,
     *  bang delta 0. Apart from the constructor, call it from one thread at
     *  a time, the audio thread once it renders. Nothing but the constructor
     *  allocates: the list is sized to the capacity and the outputs to
     *  ``STANDARD_BUFFERSIZE``; a block is at most that long (a longer one is
     *  processed up to that length).
     */
    class API zigzag {
    public:
      /** @brief What happens at the end of the range. */
      enum LOOP : int {
        LOOP_OFF = 0,
        LOOP_ON = 1,
        LOOP_PENDULUM = 2,
      };

      /** @brief What a bang and an input signal do; see the class
       *         description. */
      enum MODE : int {
        START = 0,
        TRIGGER = 1,
        STEP = 2,
      };

      /** @brief The capacity of a default-constructed function, Max's
       *         default ``maxpoints``. */
      static constexpr UInt DEFAULT_POINTS = 2048;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_POINTS = 65536;

      /** @brief A function with room for @p capacity points (1 to
       *         ``MAX_POINTS``). Allocates. */
      explicit zigzag(UInt capacity = DEFAULT_POINTS);

      /** @brief How many points the list can hold. */
      UInt capacity() const {
        return static_cast<UInt>(values.size());
      }

      /** @brief How many points the list holds. */
      UInt size() const {
        return count;
      }

      /** @brief The value of point @p index; 0 past the end. */
      Flt pointValue(UInt index) const {
        return index < count ? values[index] : 0.f;
      }

      /** @brief The delta (ms) of point @p index; 0 past the end. */
      Flt pointDelta(UInt index) const {
        return index < count ? deltas[index] : 0.f;
      }

      /**
       *  @brief Replaces the list with @p length points and stops.
       *
       *  A list longer than the capacity is cut to it. Returns how many
       *  points were taken; 0, changing nothing, when a value is not a
       *  finite number or a pointer is null.
       */
      UInt setLine(const Flt* newValues, const Flt* newDeltas, UInt length);

      /** @brief ``setLine()``, then ``play()`` when points were taken. */
      UInt line(const Flt* newValues, const Flt* newDeltas, UInt length);

      /** @brief Empties the list and stops. */
      zigzag& clear();

      /**
       *  @brief Adds a point at the end; with @p trigger (as Max does),
       *         ramps to it over @p ms.
       *
       *  False, changing nothing, when the list is full or @p value is not
       *  a finite number.
       */
      bool append(Flt value, Flt ms, bool trigger = true);

      /** @brief Inserts a point before @p index (at the end past it). False
       *         when the list is full or @p value is not finite. */
      bool insert(UInt index, Flt value, Flt ms);

      /** @brief Removes point @p index. False past the end. The running
       *         ramp then heads for the point that took its place. */
      bool remove(UInt index);

      /** @brief Sets the value and delta of point @p index without playing.
       *         False past the end or when @p value is not finite. */
      bool set(UInt index, Flt value, Flt ms);

      /** @brief Starts the list: from the current output to the start of
       *         the range over its delta, or to its end at once backward. */
      zigzag& play();

      /** @brief Jumps or steps as the mode says; see the class
       *         description. */
      zigzag& bang();

      /** @brief Ramps to point @p index (clamped to the list) over @p ms
       *         and plays on from there. */
      zigzag& jump(Int index, Flt ms = 0.f);

      /** @brief ``jump()`` to the start of the range. */
      zigzag& jumpStart(Flt ms = 0.f);

      /** @brief ``jump()`` to the end of the range. */
      zigzag& jumpEnd(Flt ms = 0.f);

      /** @brief ``jump()`` @p points on from the current index (back when
       *         negative). */
      zigzag& skip(Int points, Flt ms = 0.f);

      /** @brief ``skip(1, ms)``. */
      zigzag& next(Flt ms = 0.f) {
        return skip(1, ms);
      }

      /** @brief ``skip(-1, ms)``. */
      zigzag& prev(Flt ms = 0.f) {
        return skip(-1, ms);
      }

      /** @brief Holds the output where it is. */
      zigzag& stop();

      /** @brief Sets the output at once and stops. A value that is not a
       *         finite number is ignored. */
      zigzag& hold(Flt value);

      /** @brief The time ``bang()`` takes to reach its point. Default 0. */
      zigzag& bangDelta(Flt ms);
      Flt bangDelta() const {
        return bangMs;
      }

      /** @brief The first point of the range. Default 0. */
      zigzag& start(UInt index);

      /** @brief The last point of the range; negative for the last point
       *         of the list (the default). */
      zigzag& end(Int index);

      /** @brief ``start()`` and ``end()`` together. */
      zigzag& bound(UInt first, Int last) {
        start(first);
        return end(last);
      }

      /** @brief The play rate: 1 as written, negative backward, 0 holds. A
       *         value that is not a finite number is ignored. */
      zigzag& speed(Flt rate);
      Flt speed() const {
        return rate;
      }

      /** @brief What happens at the end of the range. Default
       *         ``LOOP_OFF``. Leaving ``LOOP_PENDULUM`` plays in the speed's
       *         direction again. */
      zigzag& loopMode(LOOP mode);
      LOOP loopMode() const {
        return loop;
      }

      /** @brief What a bang and an input signal do. Default ``START``. */
      zigzag& mode(MODE value);
      MODE mode() const {
        return trigger;
      }

      /** @brief The value of the last sample. */
      Flt value() const {
        return current;
      }

      /** @brief The current index: the point the running ramp heads for,
       *         else the point last reached. */
      UInt index() const {
        return running ? target : reached;
      }

      /** @brief True while a ramp runs. */
      bool active() const {
        return running;
      }

      /** @brief How many times play reached the end of the range (and
       *         stopped, looped or turned) in the last block call. */
      UInt ends() const {
        return endCount;
      }

      /** @brief True when ``ends()`` is not 0. */
      bool finished() const {
        return endCount > 0;
      }

      /** @brief One block of the function, played. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /**
       *  @brief One block with an input signal and a speed signal, either
       *         of them null for none.
       *
       *  @p input is an index in ``START`` mode and a trigger in the
       *  others. @p rateSignal's first sample sets the speed.
       */
      buffer& operator()(buffer* input, buffer* rateSignal, UInt length = STANDARD_BUFFERSIZE);

      /** @brief The index of each sample of the last block. */
      buffer& indices() {
        return indexOutput;
      }

      /** @brief Back to the state after construction, keeping the
       *         capacity. */
      zigzag& reset();

    private:
      Flt Step();
      bool Advance(); // after reaching `target`; false when play stopped
      void Go(Int index, Flt ms);
      int Direction() const {
        return sign * pendulum;
      }
      UInt Low() const;
      UInt High() const;

      std::vector<Flt> values;
      std::vector<Flt> deltas;
      UInt count = 0;
      UInt first = 0;
      Int last = -1;
      Flt rate = 1.f;
      int sign = 1; // of the last speed that was not 0
      int pendulum = 1;
      LOOP loop = LOOP_OFF;
      MODE trigger = START;
      Flt bangMs = 0.f;
      bool running = false;
      UInt target = 0;
      UInt reached = 0;
      Flt from = 0.f; // the running ramp's start
      Flt current = 0.f;
      Dbl duration = 0.0; // ms, the running ramp's
      Dbl elapsed = 0.0; // ms into it
      UInt endCount = 0;
      bool primed = false; // the trigger input has a previous sample
      Flt previous = 0.f;
      Flt slope = 0.f;
      buffer output;
      buffer indexOutput;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_ZIGZAG_HPP
