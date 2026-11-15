/*
  ==============================================================================

    plotData.hpp
    The data and axes of a 2-D plot of one or more subplots: values set by
    list, frames of a signal, or a region of a sample buffer, handed to a
    reader on another thread through a triple buffer (issue #1171).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_PLOTDATA_HPP
#define YSE_DSP_ANALYSIS_PLOTDATA_HPP

#include <atomic>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../sampling/sampleBuffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Holds what a 2-D plot shows, for whoever draws it: the values
     *         of one or more subplots and the axes they are drawn on. The
     *         plot owns the numbers; the reader does the drawing.
     *
     *  **Subplots and points.** A plot has ``plots()`` subplots, fixed by
     *  the constructor, and each shows ``points()`` values (4 by default)
     *  spread across its domain. Every subplot stores ``capacity()``
     *  values; the first ``points()`` of them are shown, so a value past
     *  ``points()`` is kept and shows again when ``points()`` grows.
     *
     *  **Where the values come from.** Each subplot takes its values from
     *  whichever of these is called for it:
     *
     *  - ``set()``: numbers, written from a point on (the first by
     *    default) and cut at ``capacity()``.
     *  - The signal call, a block at a time: a frame of ``frameSize()``
     *    samples (``points()`` when that is 0 or smaller) whose first
     *    ``points()`` samples become the values once the frame is complete.
     *  - The sample-buffer call, a block at a time: a frame reads
     *    ``points()`` samples of a region of the buffer, point ``i`` the
     *    sample at ``start + floor(i * length / points())``, where the region
     *    is ``region()``'s channel from ``start`` for ``length`` samples.
     *
     *  Frames start ``frameRate()`` times a second (15 by default): the
     *  wait between two frame starts is ``SAMPLERATE / frameRate()``
     *  samples, taken from the live sample rate when each wait begins, with
     *  the fraction kept, so frame ``k`` starts on the first sample at or
     *  after ``k * SAMPLERATE / frameRate()``. A frame longer than the wait
     *  is followed by the next one at once, and the clock then counts from
     *  that frame. A buffer frame reads the region the moment it starts;
     *  several due within one block read it once. Changing ``points()`` or
     *  ``frameSize()``, or switching a subplot between the signal and the
     *  buffer call, drops the signal frame being collected and starts the
     *  next frame with the next sample. NaN samples count as 0; infinities
     *  are kept.
     *
     *  **Axes.** Each subplot has a domain (the horizontal span its points
     *  are spread across, 0 to 1 by default), a range (the vertical span
     *  of its values, -1 to 1 by default), each linear or logarithmic, an X
     *  and a Y origin (0) and a filter: ``NONE``, or ``ATODB``, which shows
     *  every value as ``AmpToDb`` of it. The values are stored as they came;
     *  the filter applies to what is published. The plot does not check an
     *  axis: an empty span, or a logarithmic one that reaches 0, is the
     *  reader's to draw as it sees fit.
     *
     *  **Reading.** Changes are published by ``publish()``, which copies
     *  the subplots into a triple buffer when anything changed since the
     *  last one: three frames of the plot live in memory allocated by the
     *  constructor, the writer copies into one, the reader holds one, and
     *  the third is the newest. Publishing and taking a frame are each one
     *  atomic exchange, so neither side waits or allocates, and a frame the
     *  reader holds is never written until the reader lets go of it by
     *  acquiring another. ``acquire()`` takes the newest frame (or keeps the
     *  one it holds when none is newer); ``held()`` looks at the one it
     *  holds. The constructor publishes the first frame, so a reader always
     *  has one.
     *
     *  Nothing allocates after the constructor, which takes
     *  ``5 * plots() * capacity()`` values of memory.
     *
     *  **Threads.** One thread writes — the setters, ``set()``, the block
     *  calls, ``publish()`` — and one other thread at a time reads with
     *  ``acquire()`` and ``held()``. They may be the same thread.
     */
    class API plotData {
    public:
      /** @brief How an axis is spread. */
      enum SCALE {
        LINEAR, ///< evenly
        LOG, ///< logarithmically
      };

      /** @brief What a subplot's published values are. */
      enum FILTER {
        NONE, ///< the values as they are
        ATODB, ///< ``AmpToDb`` of every value: decibels full scale
      };

      /** @brief One axis of a subplot: from ``start`` to ``end``. */
      struct axis {
        Flt start = 0.f;
        Flt end = 1.f;
        SCALE scale = LINEAR;
      };

      /** @brief One subplot of a frame, as the reader sees it. ``values``
       *         points at the frame's ``points`` values, filtered. */
      struct subplot {
        axis domain;
        axis range;
        Flt xOrigin = 0.f;
        Flt yOrigin = 0.f;
        FILTER filter = NONE;
        const Flt* values = nullptr;
      };

      /** @brief One published frame of the whole plot.
       *
       *  ``serial`` counts the frames published, from 1. The pointers stay
       *  valid, and the values do not change, until the reader's next
       *  ``acquire()``.
       */
      struct frame {
        UInt plots = 0;
        UInt points = 0;
        UInt serial = 0;
        const subplot* subplots = nullptr;
      };

      /** @brief The points shown by default (Max's ``numpoints``). */
      static constexpr UInt DEFAULT_POINTS = 4;
      /** @brief The most points a subplot stores, and the largest capacity. */
      static constexpr UInt MAX_POINTS = 4096;
      /** @brief The most subplots a plot has. */
      static constexpr UInt MAX_PLOTS = 16;
      /** @brief The longest frame, in samples. */
      static constexpr UInt MAX_FRAME_SIZE = 1u << 22;
      /** @brief Frames a second by default (Max's ``audioframerate``). */
      static constexpr Flt DEFAULT_FRAME_RATE = 15.f;
      /** @brief The fewest frames a second. */
      static constexpr Flt MIN_FRAME_RATE = 0.1f;
      /** @brief The most frames a second. */
      static constexpr Flt MAX_FRAME_RATE = 1000.f;

      /** @brief A plot of @p plots subplots (1 to ``MAX_PLOTS``), each
       *         storing up to @p capacity values (1 to ``MAX_POINTS``).
       *         ``points()`` starts at ``DEFAULT_POINTS``, or the capacity
       *         when that is smaller. Allocates, and publishes the first
       *         frame. */
      explicit plotData(UInt plots = 1, UInt capacity = MAX_POINTS);

      plotData(const plotData&) = delete;
      plotData& operator=(const plotData&) = delete;

      /** @brief The number of subplots, fixed by the constructor. */
      UInt plots() const;
      /** @brief The values a subplot stores, fixed by the constructor. */
      UInt capacity() const;

      /** @brief Sets the values each subplot shows, clamped to 1 to
       *         ``capacity()``. A change drops the signal frames being
       *         collected. */
      plotData& points(UInt count);
      /** @brief The values each subplot shows. */
      UInt points() const;

      /** @brief Sets the samples a signal frame takes in; 0 (the default)
       *         or anything up to ``points()`` is ``points()``. Clamped to
       *         ``MAX_FRAME_SIZE``. A change drops the signal frames being
       *         collected. */
      plotData& frameSize(UInt samples);
      /** @brief The samples a signal frame takes in, as set. */
      UInt frameSize() const;

      /** @brief Sets the frames a second, clamped to ``MIN_FRAME_RATE`` to
       *         ``MAX_FRAME_RATE``; NaN is ignored. Applies from the next
       *         wait. */
      plotData& frameRate(Flt perSecond);
      /** @brief The frames a second. */
      Flt frameRate() const;

      /** @brief Writes @p count values into subplot @p plot from point
       *         @p from on; values past ``capacity()`` are dropped, NaN is
       *         0. A plot past ``plots()`` is ignored. */
      plotData& set(UInt plot, const Flt* values, UInt count, UInt from = 0);

      /** @brief Subplot @p plot's stored value @p index, unfiltered; 0
       *         outside the plot. */
      Flt value(UInt plot, UInt index) const;

      /** @brief Sets subplot @p plot's domain. NaN leaves an end as it is.
       */
      plotData& domain(UInt plot, Flt start, Flt end, SCALE scale = LINEAR);
      /** @brief Subplot @p plot's domain (the default outside the plot). */
      axis domain(UInt plot) const;

      /** @brief Sets subplot @p plot's range. NaN leaves an end as it is. */
      plotData& range(UInt plot, Flt start, Flt end, SCALE scale = LINEAR);
      /** @brief Subplot @p plot's range (the default outside the plot). */
      axis range(UInt plot) const;

      /** @brief Sets subplot @p plot's X origin, a domain value. NaN is
       *         ignored. */
      plotData& xOrigin(UInt plot, Flt value);
      /** @brief Subplot @p plot's X origin. */
      Flt xOrigin(UInt plot) const;

      /** @brief Sets subplot @p plot's Y origin, a range value. NaN is
       *         ignored. */
      plotData& yOrigin(UInt plot, Flt value);
      /** @brief Subplot @p plot's Y origin. */
      Flt yOrigin(UInt plot) const;

      /** @brief Sets what subplot @p plot's published values are. */
      plotData& filter(UInt plot, FILTER which);
      /** @brief Subplot @p plot's filter. */
      FILTER filter(UInt plot) const;

      /** @brief Sets the region the buffer call reads for subplot @p plot:
       *         channel @p channel (from 0), from @p startMs for the rest
       *         of the buffer or, when @p endMs is past @p startMs, up to
       *         @p endMs, in milliseconds at the buffer's own rate. Below 0
       *         (and NaN) is 0. Applies from the next buffer frame. */
      plotData& region(UInt plot, UInt channel, Flt startMs = 0.f, Flt endMs = 0.f);
      /** @brief The channel the buffer call reads for subplot @p plot. */
      UInt regionChannel(UInt plot) const;
      /** @brief The start of that region in milliseconds. */
      Flt regionStart(UInt plot) const;
      /** @brief The end of that region in milliseconds; at or before the
       *         start means the end of the buffer. */
      Flt regionEnd(UInt plot) const;

      /** @brief Collects a block of a signal into subplot @p plot's
       *         frames. A plot past ``plots()`` is ignored. */
      void operator()(UInt plot, buffer& in);

      /** @brief Runs @p samples samples of subplot @p plot's frame clock,
       *         reading the region of @p view into its values when a frame
       *         is due. An empty view, or a channel it lacks, reads 0. A
       *         plot past ``plots()`` is ignored. */
      void operator()(UInt plot, const sampleView& view, UInt samples);

      /** @brief Drops subplot @p plot's signal frame being collected; its
       *         next frame starts with the next sample. */
      plotData& restart(UInt plot);

      /** @brief Publishes the subplots when anything changed since the
       *         last publish; true when it did. The writing thread. */
      bool publish();

      /** @brief How many frames have been published. Any thread. */
      UInt frames() const;

      /** @brief Reader: takes the newest frame, or keeps the one held when
       *         none is newer, and returns it. */
      frame acquire();

      /** @brief Reader: the frame the last ``acquire()`` returned (before
       *         the first, the one the constructor published). */
      frame held() const;

      // --- Helpers for callers that hold settings as floats (the patcher
      // wrapper); any thread. A number's whole part, clamped as the setters
      // clamp; NaN is the default.

      /** @brief @p value as points: its whole part, 1 to ``MAX_POINTS``;
       *         NaN is ``DEFAULT_POINTS``. */
      static UInt Points(Flt value);
      /** @brief @p value as subplots: its whole part, 1 to ``MAX_PLOTS``;
       *         NaN is 1. */
      static UInt Plots(Flt value);
      /** @brief @p value as a frame size: its whole part, 0 to
       *         ``MAX_FRAME_SIZE``; NaN is 0. */
      static UInt FrameSize(Flt value);

    private:
      enum SOURCE {
        LISTS, // set() only
        SIGNAL,
        SAMPLES, // a sample buffer
      };

      struct state {
        axis domain;
        axis range;
        Flt xOrigin = 0.f;
        Flt yOrigin = 0.f;
        FILTER filter = NONE;
        UInt channel = 0;
        Flt startMs = 0.f;
        Flt endMs = 0.f;
        // The frame clock.
        SOURCE source = LISTS;
        Dbl wait = 0.0; // samples until the next frame may start
        UInt filled = 0; // samples of the signal frame so far
        bool collecting = false;
      };

      struct slot {
        UInt points = 0;
        UInt serial = 0;
      };

      static constexpr unsigned int FRESH = 4;
      static constexpr unsigned int INDEX = 3;

      // Starts a frame: the wait to the next one, from the live rate.
      void begin(state& s) const;
      void use(UInt plot, SOURCE source);
      void restartAll();
      Flt* live(UInt plot);
      const Flt* live(UInt plot) const;
      Flt* captured(UInt plot);

      UInt numPlots;
      UInt cap;
      std::vector<Flt> values; // the live values, plots * cap
      std::vector<Flt> capture; // the signal frames being collected, plots * cap
      std::vector<state> states;

      // The triple buffer: three slots of the subplots and their values.
      std::vector<Flt> store; // three slots of plots * cap values
      std::vector<subplot> shown; // three slots of plots subplots
      slot slots[3];

      // Settings, the writing thread's.
      UInt numPoints;
      UInt samplesPerFrame;
      Flt rate;

      bool dirty;
      UInt serial;
      unsigned int writeSlot;

      // The newest slot, | FRESH while no reader has taken it.
      std::atomic<unsigned int> published;
      std::atomic<UInt> count;
      unsigned int readSlot; // the reader's
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_PLOTDATA_HPP
