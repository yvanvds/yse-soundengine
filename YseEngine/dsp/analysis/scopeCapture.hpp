/*
  ==============================================================================

    scopeCapture.hpp
    An oscilloscope's capture: traces of a signal, or of an X/Y pair, with
    samples-per-point decimation and triggering, handed to a reader on
    another thread through a triple buffer (issue #1169).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_SCOPECAPTURE_HPP
#define YSE_DSP_ANALYSIS_SCOPECAPTURE_HPP

#include <atomic>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Captures an oscilloscope's traces of a signal, or of an X/Y
     *         pair of signals, for whoever draws them. The capture owns the
     *         numbers; the reader does the drawing.
     *
     *  **Traces.** A trace is ``size()`` points (128 by default), and each
     *  point stands for ``period()`` samples (256 by default), so a trace
     *  covers ``size() * period()`` samples. A trace starts on a sample and
     *  ends on a sample, sample-accurate across blocks.
     *
     *  - **Waveform** (the one-signal call): a point holds the minimum and
     *    the maximum of its samples, so the reader can draw every peak the
     *    decimation would otherwise hide.
     *  - **X/Y** (the two-signal call): a point holds the first X sample of
     *    its period and the Y sample beside it, for a Lissajous figure.
     *
     *  NaN samples count as 0; infinities are kept.
     *
     *  **Between traces.** When a trace ends the capture waits ``delay()``
     *  milliseconds (0 by default), turned into whole samples with the live
     *  ``SAMPLERATE`` when the wait begins. It then waits for the trigger:
     *  with ``NONE`` (the default) the next sample starts the next trace;
     *  with ``UP`` the first sample at or above ``level()`` whose previous
     *  sample was below it does; with ``DOWN`` the first at or below the
     *  level after one above it. The trigger watches the waveform signal,
     *  or X. The samples it watches include the delay's, so a crossing
     *  needs one sample on each side of the level; the first sample after
     *  construction has none before it and never triggers.
     *
     *  Changing ``size()`` or ``period()``, or switching between the
     *  waveform and the X/Y call, drops the trace being captured: the next
     *  one starts from the trigger, without the delay. ``restart()`` does
     *  the same.
     *
     *  **Reading.** Every finished trace is published to a triple buffer:
     *  three traces live in memory allocated by the constructor, the
     *  capture writes its samples straight into one, the reader holds one,
     *  and the third is the newest finished trace. Publishing and taking a
     *  trace are each one atomic exchange, so neither side copies, waits or
     *  allocates, and a trace the reader holds is never written until the
     *  reader lets go of it by acquiring another. ``acquire()`` takes the
     *  newest finished trace (or keeps the one it holds when none is newer);
     *  ``held()`` looks at the one it holds. A reader that polls slower than
     *  traces finish skips the ones in between.
     *
     *  The capacity is the largest ``size()``, fixed by the constructor; the
     *  three traces take ``6 * capacity`` samples of memory. Nothing
     *  allocates afterwards.
     *
     *  **Threads.** One thread captures — the block calls, the setters,
     *  ``restart()`` — and one other thread at a time reads with
     *  ``acquire()`` and ``held()``. They may be the same thread.
     */
    class API scopeCapture {
    public:
      /** @brief What a trace's points hold. */
      enum MODE {
        WAVEFORM, ///< a point's minimum and maximum sample
        XY, ///< a point's first X sample and the Y sample beside it
      };

      /** @brief When a new trace starts, after the delay. */
      enum TRIGGER {
        NONE, ///< at once
        UP, ///< when the signal rises from below the level to at or above it
        DOWN, ///< when the signal falls from above the level to at or below it
      };

      /** @brief One finished trace, as the reader sees it.
       *
       *  ``first`` and ``second`` point at ``size`` values each: the
       *  minimum and maximum of every point in ``WAVEFORM`` mode, X and Y
       *  in ``XY`` mode. ``serial`` counts the traces the capture has
       *  finished, from 1; a trace with ``serial`` 0 is none (``size`` 0,
       *  null pointers). The pointers stay valid, and the values do not
       *  change, until the reader's next ``acquire()``.
       */
      struct trace {
        MODE mode = WAVEFORM;
        UInt size = 0;
        UInt serial = 0;
        const Flt* first = nullptr;
        const Flt* second = nullptr;
      };

      /** @brief The points of a trace by default (Max's ``bufsize``). */
      static constexpr UInt DEFAULT_SIZE = 128;
      /** @brief The samples per point by default (Max's ``calccount``). */
      static constexpr UInt DEFAULT_PERIOD = 256;
      /** @brief The fewest points a trace has. */
      static constexpr UInt MIN_SIZE = 2;
      /** @brief The largest capacity, and so the most points a trace has. */
      static constexpr UInt MAX_SIZE = 8192;
      /** @brief The fewest samples a point stands for. */
      static constexpr UInt MIN_PERIOD = 2;
      /** @brief The most samples a point stands for. */
      static constexpr UInt MAX_PERIOD = 8192;

      /** @brief A capture of up to @p capacity points a trace, clamped to
       *         ``MIN_SIZE`` to ``MAX_SIZE``. ``size()`` starts at
       *         ``DEFAULT_SIZE``, or the capacity when that is smaller.
       *         Allocates. */
      explicit scopeCapture(UInt capacity = MAX_SIZE);

      scopeCapture(const scopeCapture&) = delete;
      scopeCapture& operator=(const scopeCapture&) = delete;

      /** @brief Sets the points a trace has, clamped to ``MIN_SIZE`` to
       *         ``capacity()``. A change drops the trace being captured. */
      scopeCapture& size(UInt points);
      /** @brief The points a trace has. */
      UInt size() const;

      /** @brief Sets the samples a point stands for, clamped to
       *         ``MIN_PERIOD`` to ``MAX_PERIOD``. A change drops the trace
       *         being captured. */
      scopeCapture& period(UInt samples);
      /** @brief The samples a point stands for. */
      UInt period() const;

      /** @brief Sets the wait between the end of a trace and the trigger,
       *         in milliseconds. Below 0 is 0; NaN is ignored. Applies from
       *         the next wait. */
      scopeCapture& delay(Flt ms);
      /** @brief The wait between traces in milliseconds. */
      Flt delay() const;

      /** @brief Sets when a new trace starts after the delay. */
      scopeCapture& trigger(TRIGGER when);
      /** @brief When a new trace starts after the delay. */
      TRIGGER trigger() const;

      /** @brief Sets the level the ``UP`` and ``DOWN`` triggers watch for.
       *         NaN is ignored. */
      scopeCapture& level(Flt value);
      /** @brief The trigger level. */
      Flt level() const;

      /** @brief The largest ``size()``, fixed by the constructor. */
      UInt capacity() const;

      /** @brief Captures a block of one signal as waveform points. */
      void operator()(buffer& in);

      /** @brief Captures a block of an X/Y pair as X/Y points. Both buffers
       *         are read up to the shorter of the two. */
      void operator()(buffer& x, buffer& y);

      /** @brief Drops the trace being captured; the next one starts from
       *         the trigger, without the delay. Published traces stay. */
      scopeCapture& restart();

      /** @brief How many traces have finished. Any thread. */
      UInt traces() const;

      /** @brief Reader: takes the newest finished trace, or keeps the one
       *         held when none is newer, and returns it. A trace with
       *         ``serial`` 0 until the first one finishes. */
      trace acquire();

      /** @brief Reader: the trace the last ``acquire()`` returned. */
      trace held() const;

      // --- Helpers for callers that hold settings as floats (the patcher
      // wrapper); any thread. A number's whole part, clamped as the setters
      // clamp; NaN is the default.

      /** @brief @p value as points: its whole part, ``MIN_SIZE`` to
       *         ``MAX_SIZE``; NaN is ``DEFAULT_SIZE``. */
      static UInt Size(Flt value);
      /** @brief @p value as samples per point: its whole part,
       *         ``MIN_PERIOD`` to ``MAX_PERIOD``; NaN is
       *         ``DEFAULT_PERIOD``. */
      static UInt Period(Flt value);
      /** @brief @p value as a trigger, Max's numbering: whole part 1 is
       *         ``UP``, 2 is ``DOWN``, anything else ``NONE``. */
      static TRIGGER Trigger(Flt value);

    private:
      enum PHASE {
        WAITING, // the delay after a trace
        ARMED, // waiting for the trigger
        CAPTURING, // filling a trace
      };

      struct slot {
        MODE mode = WAVEFORM;
        UInt size = 0;
        UInt serial = 0;
      };

      static constexpr unsigned int FRESH = 4;
      static constexpr unsigned int INDEX = 3;

      // One sample of the signal (a), and of Y (b) in X/Y mode.
      void step(Flt a, Flt b, bool xy);
      void publish();
      void setMode(MODE wanted);
      Flt* first(unsigned int s);
      Flt* second(unsigned int s);
      const Flt* first(unsigned int s) const;
      const Flt* second(unsigned int s) const;

      std::vector<Flt> store; // three slots of 2 * cap values
      UInt cap;
      slot slots[3];

      // Settings, the capturing thread's.
      UInt points;
      UInt samples;
      Flt delayMs;
      TRIGGER when;
      Flt threshold;

      // Capture state, the capturing thread's.
      MODE mode;
      PHASE phase;
      U64 waitLeft;
      UInt point; // the point being filled
      UInt inPeriod; // samples of it so far
      Flt low;
      Flt high;
      Flt previous; // the last watched sample, NaN before the first
      UInt serial;
      unsigned int writeSlot;

      // The newest finished slot, | FRESH while no reader has taken it.
      std::atomic<unsigned int> published;
      std::atomic<UInt> finished;
      unsigned int readSlot; // the reader's
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_SCOPECAPTURE_HPP
