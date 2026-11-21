/*
  ==============================================================================

    waveformView.hpp
    The view of a sample buffer a waveform display shows and edits: the part
    on screen, the selection, the mouse gestures that change them, and the
    samples a drawing gesture writes (issue #1151).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_WAVEFORMVIEW_HPP
#define YSE_DSP_SAMPLING_WAVEFORMVIEW_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "sampleBuffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The state of a waveform display of one channel of a sample
     *         buffer: which part is shown, which part is selected, and what
     *         a mouse gesture does to them. The view owns the numbers; whoever
     *         draws reads them, and reads the samples themselves with
     *         ``sampleBuffer::peaks``.
     *
     *  **Times.** Every time is in milliseconds at the buffer's own rate,
     *  from its first sample. ``shape()`` tells the view how long the buffer
     *  is; a ``mouse()`` call with samples takes the shape from them. Before
     *  any shape the buffer is empty and the rate is the engine's
     *  (``SAMPLERATE``), read when used.
     *
     *  **Display.** The display shows ``displayLength()`` milliseconds from
     *  ``displayStart()``. By default, and after ``displayLength(0)``, the
     *  length is the whole buffer, and follows it when the buffer changes
     *  (``wholeBuffer()``).
     *
     *  **Selection.** A start and an end, as set: an inlet may set the end
     *  before the start. A mouse gesture leaves them in order. ``undo()``
     *  swaps the selection with the one before the last change, so two undos
     *  return to where they began.
     *
     *  **Mouse.** ``mouse()`` takes a position across the display, ``x`` from
     *  0 (left edge) to 1 (right edge), and ``y`` from -1 (bottom) to 1 (top),
     *  with a state: ``DOWN``, then any number of ``DRAG``, then ``UP``. What
     *  it does depends on ``mode()``:
     *
     *  - ``NONE``: nothing but the report of the mouse.
     *  - ``SELECT``: the selection runs from where the button went down to
     *    where the mouse is.
     *  - ``LOOP``: the edge of the selection nearer to where the button went
     *    down moves with the mouse; the other stays.
     *  - ``MOVE``: dragging sideways scrolls the display, so the time under
     *    the mouse when the button went down stays under it; dragging up
     *    zooms in, by a factor of 2 for every 1 of ``y``, around that time
     *    (down zooms in instead with ``zoomDown(true)``). The display is
     *    never shorter than one sample nor starts before 0.
     *  - ``DRAW``: writes the amplitude under the mouse into the samples
     *    (``mouse()`` with a write view), and a straight line between the
     *    samples of successive positions, so a fast drag leaves no gaps.
     *    The edit is in place and cannot be undone.
     *
     *  A selection time is snapped by ``snap()``: to the grid (every
     *  ``gridSpacing()`` milliseconds from ``offset()``), to ticks (the
     *  grid divided by ``ticks()``), or to the nearest zero crossing of the
     *  channel within ``MAX_ZERO_SEARCH`` samples, then held within the
     *  buffer.
     *
     *  The amplitude at ``y`` is ``(y - verticalOffset()) /
     *  verticalZoom()``: the zero line is drawn at ``verticalOffset()``, and
     *  ``verticalZoom()`` magnifies. With ``clipDraw(true)`` drawn values are
     *  held within the amplitudes the display shows, ``y`` from -1 to 1.
     *
     *  **What to send.** Every call that changes the view returns an
     *  ``event``: whether the display, the selection, the four numbers
     *  together (the link) or the mouse should be reported, whether samples
     *  were written, and whether the tempo changed. ``output()`` says when a
     *  gesture reports the selection (on the button going down, coming up,
     *  both, or at every move); with ``quiet(true)`` nothing but the mouse is
     *  reported during a drag, and a display or link that changed during it
     *  is reported when the button comes up. The link is reported whenever one of the four numbers
     *  changed, and never when none did, so views that feed each other's
     *  links in a circle settle instead of feeding back.
     *
     *  **Units.** ``unit()`` is what the grid is counted in: milliseconds,
     *  samples, a phase of the buffer's length (0 to 1), or bars of
     *  ``beats()`` beats at ``tempo()`` beats a minute (``BPM``, where the
     *  grid number is ignored and a bar is one grid step). ``line()`` places
     *  a play position, given in ``lineUnit()`` (in ``BPM``, beats from
     *  ``offset()``) and kept in milliseconds, converted when it is set.
     *  ``fitTempo()`` sets the tempo so the selection is one bar, starts the
     *  bars on the selection start, and switches the unit to ``BPM``.
     *
     *  Nothing allocates, locks or blocks: every call is safe on the audio
     *  thread. One thread at a time.
     */
    class API waveformView {
    public:
      /** @brief What a mouse gesture does (Max's ``setmode``). */
      enum MODE {
        NONE, ///< nothing: display only
        SELECT, ///< select from the click to the mouse
        LOOP, ///< move the nearer selection edge
        MOVE, ///< scroll and zoom the display
        DRAW, ///< write the samples
      };

      /** @brief When a gesture reports the selection (Max's ``outmode``). */
      enum OUTPUT {
        OUTPUT_NONE, ///< never
        OUTPUT_DOWN, ///< when the button goes down
        OUTPUT_UP, ///< when it comes up
        OUTPUT_DOWNUP, ///< both
        OUTPUT_CONTINUOUS, ///< down, every drag, and up
      };

      /** @brief What a selection time snaps to. */
      enum SNAP {
        SNAP_NONE, ///< nothing
        SNAP_GRID, ///< the grid
        SNAP_TICK, ///< the grid divided by ``ticks()``
        SNAP_ZERO, ///< the nearest zero crossing
      };

      /** @brief What the grid and the line are counted in. */
      enum UNIT {
        MS, ///< milliseconds
        SAMPLES, ///< samples at the buffer's rate
        PHASE, ///< a fraction of the buffer's length
        BPM, ///< bars (the grid) and beats (the line) at ``tempo()``
      };

      /** @brief A mouse state, as Max reports it. */
      enum MOUSE {
        UP = 0, ///< the button came up
        DOWN = 1, ///< the button went down
        DRAG = 2, ///< the mouse moved with the button down
      };

      /** @brief What a call changed, and so what to report. */
      struct event {
        /** @brief Report the display start and length. */
        bool display = false;
        /** @brief Report the selection start and end. */
        bool selection = false;
        /** @brief Report all four numbers together. */
        bool link = false;
        /** @brief Report the mouse: ``x``, ``y`` and ``state``. */
        bool mouse = false;
        /** @brief Samples were written. */
        bool edited = false;
        /** @brief The tempo changed (``fitTempo``). */
        bool tempo = false;
        /** @brief The mouse position across the display, 0 to 1. */
        Flt x = 0.f;
        /** @brief The amplitude under the mouse. */
        Flt y = 0.f;
        /** @brief The mouse state. */
        MOUSE state = UP;
      };

      /** @brief The grid by default (Max's ``grid``). */
      static constexpr Dbl DEFAULT_GRID = 1000.0;
      /** @brief The tempo by default, in beats a minute. */
      static constexpr Dbl DEFAULT_TEMPO = 120.0;
      /** @brief The beats in a bar by default. */
      static constexpr UInt DEFAULT_BEATS = 4;
      /** @brief How far a zero-crossing snap looks, in samples each way. */
      static constexpr UInt MAX_ZERO_SEARCH = 65536;
      /** @brief The smallest vertical zoom. */
      static constexpr Flt MIN_VERTICAL_ZOOM = 1e-6f;

      waveformView() = default;

      // ─── the buffer ──────────────────────────────────────────────────────

      /** @brief The buffer's length in samples and its rate (0 or less is
       *         the engine rate). The times stay as they are. */
      waveformView& shape(UInt frames, Flt sampleRate);
      /** @brief The buffer's length in samples, as last shaped. */
      UInt frames() const;
      /** @brief The buffer's rate, as last shaped, else the engine's. */
      Flt sampleRate() const;
      /** @brief The buffer's length in milliseconds. */
      Dbl bufferLength() const;

      // ─── display and selection ───────────────────────────────────────────

      /** @brief Sets the display start; below 0 is 0, NaN is ignored. */
      event displayStart(Dbl ms);
      /** @brief The display start. */
      Dbl displayStart() const;

      /** @brief Sets the display length; 0 or less is the whole buffer, NaN
       *         is ignored. */
      event displayLength(Dbl ms);
      /** @brief The display length: the buffer's length while
       *         ``wholeBuffer()``. */
      Dbl displayLength() const;
      /** @brief Whether the display length is the whole buffer. */
      bool wholeBuffer() const;

      /** @brief Sets the selection start; below 0 is 0, NaN is ignored. */
      event selectionStart(Dbl ms);
      /** @brief The selection start. */
      Dbl selectionStart() const;

      /** @brief Sets the selection end; below 0 is 0, NaN is ignored. */
      event selectionEnd(Dbl ms);
      /** @brief The selection end. */
      Dbl selectionEnd() const;

      /** @brief Sets all four, as a linked view reports them; NaN leaves
       *         one as it is. The link is reported only when one changed. */
      event link(Dbl start, Dbl length, Dbl selectionStart, Dbl selectionEnd);

      /** @brief The whole buffer, from 0, and an empty selection at 0: what
       *         a new buffer shows. */
      event reset();

      /** @brief Swaps the selection with the one before its last change. */
      event undo();

      /** @brief One mouse event, at @p x across the display (0 to 1, held
       *         there) and @p y up it (-1 to 1). @p samples, when given, is
       *         the buffer: its shape is taken, ``DRAW`` writes into it and
       *         a zero-crossing snap reads it. */
      event mouse(Flt x, Flt y, MOUSE state, sampleWriteView* samples = nullptr);

      // ─── the line ────────────────────────────────────────────────────────

      /** @brief Places the line at @p position in ``lineUnit()``; negative
       *         or NaN hides it. */
      waveformView& line(Dbl position);
      /** @brief The line in milliseconds, or -1 when it is hidden. */
      Dbl line() const;
      /** @brief Sets what ``line()`` positions are counted in. */
      waveformView& lineUnit(UNIT which);
      /** @brief What ``line()`` positions are counted in. */
      UNIT lineUnit() const;

      // ─── settings ────────────────────────────────────────────────────────

      /** @brief Sets what a gesture does; ends a gesture in progress. */
      waveformView& mode(MODE which);
      /** @brief What a gesture does. */
      MODE mode() const;

      /** @brief Sets when a gesture reports the selection. */
      waveformView& output(OUTPUT which);
      /** @brief When a gesture reports the selection. */
      OUTPUT output() const;

      /** @brief Sets whether a drag reports nothing but the mouse. */
      waveformView& quiet(bool on);
      /** @brief Whether a drag reports nothing but the mouse. */
      bool quiet() const;

      /** @brief Sets what a selection time snaps to. */
      waveformView& snap(SNAP which);
      /** @brief What a selection time snaps to. */
      SNAP snap() const;

      /** @brief Sets the grid, in ``unit()``; NaN is ignored. */
      waveformView& grid(Dbl spacing);
      /** @brief The grid, as set. */
      Dbl grid() const;
      /** @brief The grid in milliseconds; 0 when it is none (0 or less). */
      Dbl gridSpacing() const;

      /** @brief Sets the ticks a grid step is divided into; 0 is none. */
      waveformView& ticks(UInt count);
      /** @brief The ticks a grid step is divided into. */
      UInt ticks() const;

      /** @brief Sets where the grid starts, in milliseconds; NaN is
       *         ignored. */
      waveformView& offset(Dbl ms);
      /** @brief Where the grid starts. */
      Dbl offset() const;

      /** @brief Sets what the grid is counted in. */
      waveformView& unit(UNIT which);
      /** @brief What the grid is counted in. */
      UNIT unit() const;

      /** @brief Sets the tempo in beats a minute (0 or less, or NaN, keeps
       *         it) and the beats in a bar (0 keeps them). */
      waveformView& tempo(Dbl bpm, UInt beatsPerBar = 0);
      /** @brief The tempo in beats a minute. */
      Dbl tempo() const;
      /** @brief The beats in a bar. */
      UInt beats() const;
      /** @brief The tempo that makes the selection one bar; the bars start
       *         on the selection start (``offset()``) and ``unit()`` becomes
       *         ``BPM``. Nothing for an empty selection. */
      event fitTempo();

      /** @brief Sets the vertical zoom, at least ``MIN_VERTICAL_ZOOM``; NaN
       *         is ignored. */
      waveformView& verticalZoom(Flt zoom);
      /** @brief The vertical zoom. */
      Flt verticalZoom() const;

      /** @brief Sets where the zero line is, -1 (bottom) to 1 (top) at no
       *         zoom; NaN is ignored. */
      waveformView& verticalOffset(Flt where);
      /** @brief Where the zero line is. */
      Flt verticalOffset() const;

      /** @brief Sets whether drawn values are held within what the display
       *         shows. */
      waveformView& clipDraw(bool on);
      /** @brief Whether drawn values are held within what the display
       *         shows. */
      bool clipDraw() const;

      /** @brief Sets whether dragging down, not up, zooms in. */
      waveformView& zoomDown(bool on);
      /** @brief Whether dragging down zooms in. */
      bool zoomDown() const;

      /** @brief Sets the channel shown and drawn, from 0. */
      waveformView& channel(UInt which);
      /** @brief The channel shown and drawn. */
      UInt channel() const;

      // ─── conversions ─────────────────────────────────────────────────────

      /** @brief The amplitude at @p y. */
      Flt amplitude(Flt y) const;
      /** @brief @p ms snapped by ``snap()`` and held within the buffer;
       *         @p samples is read for a zero-crossing snap. */
      Dbl snapped(Dbl ms, const sampleWriteView* samples = nullptr) const;

    private:
      Dbl toMs(Dbl value, UNIT which) const;
      UInt toFrame(Dbl ms) const;
      Dbl hold(Dbl ms) const;
      Dbl zeroCrossing(Dbl ms, const sampleWriteView& samples) const;
      // Sets the selection; true when it changed. `remember` keeps the old
      // one for undo().
      bool select(Dbl start, Dbl end, bool remember);
      bool draw(sampleWriteView& samples, UInt frame, Flt value);

      UInt numFrames = 0;
      Flt rate = 0.f;

      Dbl start = 0.0;
      Dbl length = 0.0;
      bool whole = true;
      Dbl selStart = 0.0;
      Dbl selEnd = 0.0;
      Dbl prevStart = 0.0;
      Dbl prevEnd = 0.0;
      Dbl lineMs = -1.0;
      UNIT lineIn = MS;

      MODE gesture = SELECT;
      OUTPUT outputMode = OUTPUT_CONTINUOUS;
      bool silent = false;
      SNAP snapTo = SNAP_NONE;
      Dbl gridValue = DEFAULT_GRID;
      UInt tickCount = 0;
      Dbl gridOffset = 0.0;
      UNIT gridUnit = MS;
      Dbl bpm = DEFAULT_TEMPO;
      UInt barBeats = DEFAULT_BEATS;
      Flt vZoom = 1.f;
      Flt vOffset = 0.f;
      bool clip = false;
      bool downZooms = false;
      UInt chan = 0;

      // The gesture in progress.
      bool held = false;
      Dbl anchor = 0.0; // SELECT / LOOP: the edge that stays; MOVE: the time grabbed
      Flt grabX = 0.f;
      Flt grabY = 0.f;
      Dbl grabLength = 0.0;
      UInt lastFrame = 0;
      Flt lastValue = 0.f;
      // Changed during a quiet drag, to report when the button comes up.
      bool pendingDisplay = false;
      bool pendingLink = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_WAVEFORMVIEW_HPP
