/*
  ==============================================================================

    clockFollower.hpp
    A 0..1 beat ramp locked to a tempo, tap tempo, MIDI beat clock or a click
    track (issue #1131).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_CLOCKFOLLOWER_HPP
#define YSE_DSP_GENERATORS_CLOCKFOLLOWER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "beatSpan.hpp"

#include <cstdint>

namespace YSE {
  namespace DSP {

    /**
     *  @brief A phase ramp that rises from 0 to 1 once per beat (a quarter
     *         note) of an external clock, and reports the tempo it follows.
     *
     *  The tempo comes from whichever source spoke last:
     *
     *  - **A number.** ``tempo(bpm)`` sets it directly.
     *  - **Taps.** Each ``tap()`` is a beat. Three taps whose two intervals
     *    agree within ``BEAT_TOLERANCE`` (20 %) set the tempo to 60 over their
     *    mean interval, and every further tap that agrees with the one before
     *    it updates it. An interval outside ``MIN_BEAT_TEMPO`` to
     *    ``MAX_BEAT_TEMPO`` (2 s to 0.25 s) starts the count again.
     *  - **A click track.** The signal given to ``operator()(buffer&)`` is
     *    read like taps, to the sample: a click is the absolute value rising
     *    to ``CLICK_THRESHOLD`` (0.1) after it was below, at least half the
     *    shortest beat (0.125 s) after the previous click.
     *  - **MIDI beat clock.** ``midi(248)`` is one tick, ``ppq()`` of them a
     *    beat (24 by default). The tempo is the least-squares slope through
     *    the last two beats of ticks, adopted once a sixteenth of a beat has
     *    arrived and then whenever it moves more than ``TEMPO_HYSTERESIS``
     *    (0.1 %), so the jitter of ticks that only arrive between blocks does
     *    not wobble the ramp. A gap of ``TICK_TIMEOUT`` seconds forgets the
     *    ticks before it. ``midi(250)`` (start) resets the ramp to 0 and holds
     *    it there until the next tick, which is the first beat;
     *    ``midi(251)`` (continue) and ``midi(252)`` (stop) resume and halt it.
     *    Other bytes are ignored.
     *
     *  **Phase.** Taps, clicks and ticks also say where the beat is: a tap or
     *  a click is phase 0, tick n after a MIDI start is phase (n mod ppq) /
     *  ppq. A tick that arrives without a MIDI start before it takes the
     *  nearest tick position of the ramp. When the ramp is within a quarter
     *  of the event's span (a beat for taps and clicks, a tick for MIDI) of
     *  where the event says it should be, the difference is absorbed over the
     *  next span by running a little faster or slower, so the ramp never
     *  jumps; further off, it jumps there. While stopped, events only set the
     *  tempo.
     *
     *  **Transport.** ``start()`` restarts the ramp from 0 and runs it,
     *  ``stop()`` holds it where it is. It runs from construction.
     *  ``offset()`` shifts the output phase by 0..1 of a beat.
     *
     *  **MIDI clock out.** While running, every time the ramp, shifted by
     *  ``clockOffset()``, crosses a multiple of 1 / ppq, the block reports a
     *  248, so ``ppq()`` ticks a beat follow the ramp; a start, continue or
     *  stop, from a call or from MIDI, reports 250, 251 or 252 first. The
     *  first tick after a start is reported with the ramp's first sample.
     *
     *  **Following a beat clock** (issue #1278). ``operator()(const
     *  beatSpan&)`` takes the beat from a domain clock instead: the ramp is
     *  the clock's beat position, wrapped (a ramp on beat 3.25 is at 0.25),
     *  so it rises once per beat of the clock, bends with its tempo and
     *  stands still while it is stopped. The tempo is the clock's, held to
     *  ``MIN_TEMPO``..``MAX_TEMPO`` (its size, for a clock running
     *  backwards) and taken only while the clock moves, so a stopped clock
     *  keeps the tempo it stopped at. The clock out follows the ramp as
     *  ever; the clock stopping reports 252 and moving again 251 (continue:
     *  a domain clock resumes where it stopped). Once a block has followed a
     *  clock the object keeps doing so until ``reset()``: ``tap()``,
     *  ``midi()``, a click track, ``tempo()``, ``start()`` and ``stop()``
     *  are ignored, since the clock decides both the beat and the tempo. A
     *  span without a reading holds the ramp and sends no clock.
     *
     *  **Reports.** After ``operator()``, ``taps()``, ``clicks()``,
     *  ``ticks()``, ``tempoChanged()`` and the clock bytes cover that block
     *  and the calls made since the block before it.
     *
     *  Event times are kept in seconds and the tempo in BPM, and the sample
     *  rate is read from ``SAMPLERATE`` on every block, so the ramp keeps its
     *  tempo across an engine restart at another rate. Events called between
     *  two blocks happen at the first sample of the next.
     *
     *  Call it only from one thread. Nothing allocates after the constructor:
     *  the output is sized to ``STANDARD_BUFFERSIZE``, a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples),
     *  and the tick history and the clock bytes are fixed arrays.
     */
    class API clockFollower {
    public:
      static constexpr Flt DEFAULT_TEMPO = 120.f;
      /** Tempo range in BPM; a tempo outside it is held to it. */
      static constexpr Flt MIN_TEMPO = 1.f;
      static constexpr Flt MAX_TEMPO = 1000.f;
      /** Tempo range taps and clicks are read in, in BPM. */
      static constexpr Flt MIN_BEAT_TEMPO = 30.f;
      static constexpr Flt MAX_BEAT_TEMPO = 240.f;
      /** How far two tap or click intervals may differ, relative to the
          longer one, and still agree. */
      static constexpr Flt BEAT_TOLERANCE = 0.2f;
      /** The level a click track has to reach. */
      static constexpr Flt CLICK_THRESHOLD = 0.1f;
      static constexpr UInt DEFAULT_PPQ = 24;
      static constexpr UInt MAX_PPQ = 192;
      /** Relative change before a tempo read from MIDI ticks is adopted. */
      static constexpr Dbl TEMPO_HYSTERESIS = 0.001;
      /** Seconds without a tick after which the ticks before are forgotten. */
      static constexpr Dbl TICK_TIMEOUT = 2.0;
      /** Clock bytes one block reports; more are dropped. */
      static constexpr UInt MAX_CLOCK_BYTES = 64;

      /** MIDI system real-time bytes. */
      static constexpr Int MIDI_TICK = 248;
      static constexpr Int MIDI_START = 250;
      static constexpr Int MIDI_CONTINUE = 251;
      static constexpr Int MIDI_STOP = 252;

      clockFollower();

      /** @brief One block of the ramp with no click track. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block of the ramp, reading @p click as a click track. */
      buffer& operator()(buffer& click);

      /** @brief One block following a beat clock: the ramp is the clock's
       *         beat, wrapped, for ``clock.length()`` samples (issue #1278). */
      buffer& operator()(const beatSpan& clock);

      /** @brief Whether the object follows a beat clock (it has been given a
       *         ``beatSpan`` since the last ``reset()``). */
      bool following() const {
        return isFollowing;
      }

      /** @brief Start over at @p bpm: the ramp at 0 and running, no taps,
       *         clicks or ticks remembered, not following a clock, nothing to
       *         report. ``offset()``, ``clockOffset()`` and ``ppq()`` are
       *         kept. */
      clockFollower& reset(Flt bpm = DEFAULT_TEMPO);

      /** @brief Set the tempo in BPM, held to MIN_TEMPO..MAX_TEMPO. A NaN
       *         is ignored. The phase is kept. */
      clockFollower& tempo(Flt bpm);
      Flt tempo() const;

      /** @brief Shift the output by this much of a beat, wrapped into 0..1. */
      clockFollower& offset(Flt phase);
      Flt offset() const;

      /** @brief Shift the MIDI clock out by this much of a beat, wrapped into
       *         0..1. */
      clockFollower& clockOffset(Flt phase);
      Flt clockOffset() const;

      /** @brief Ticks per beat, in and out: held to 1..MAX_PPQ. A change
       *         forgets the ticks received so far. */
      clockFollower& ppq(UInt ticks);
      UInt ppq() const;

      /** @brief Restart the ramp from 0 and run it; reports 250. */
      clockFollower& start();
      /** @brief Hold the ramp where it is; reports 252. */
      clockFollower& stop();
      bool running() const;

      /** @brief One tap: a beat, now. */
      clockFollower& tap();

      /** @brief One MIDI byte: 248 tick, 250 start, 251 continue, 252 stop;
       *         anything else is ignored. */
      clockFollower& midi(Int status);

      /** @brief The phase of the ramp's next sample before ``offset()``. */
      Flt phase() const;

      /** @brief Taps in the last report. */
      UInt taps() const {
        return reportTaps;
      }
      /** @brief Clicks found in the last report. */
      UInt clicks() const {
        return reportClicks;
      }
      /** @brief MIDI ticks received in the last report. */
      UInt ticks() const {
        return reportTicks;
      }
      /** @brief Whether the tempo changed in the last report. */
      bool tempoChanged() const {
        return reportTempo;
      }
      /** @brief MIDI clock bytes out in the last report, in order. */
      UInt clockBytes() const {
        return clockCount;
      }
      Int clockByte(UInt index) const;

    private:
      static constexpr UInt TICK_HISTORY = 2 * MAX_PPQ + 1;

      buffer& process(const Flt* click, UInt length);
      void openReport();
      void pushClockByte(Int value);
      void adoptTempo(Dbl bpm);
      void beat(Dbl* history, UInt& count, Dbl time);
      void tick(Dbl time);
      void align(Dbl target, Dbl span, Dbl samples);
      Int clockIndex() const;

      buffer output;

      Dbl bpm = DEFAULT_TEMPO;
      Dbl phaseNow = 0.0;
      Dbl outputOffset = 0.0;
      Dbl midiOffset = 0.0;
      UInt ticksPerBeat = DEFAULT_PPQ;
      bool isRunning = true;
      // Following a beat clock (issue #1278): taps, ticks and the rest are
      // ignored until reset().
      bool isFollowing = false;
      // After a MIDI start: the ramp holds at 0 until the next tick.
      bool waitForTick = false;

      // A difference absorbed by running faster or slower for a while.
      Dbl correction = 0.0;
      UInt correctionLeft = 0;

      // Seconds at the start of the next sample.
      Dbl now = 0.0;

      Dbl tapTimes[3] = {0.0, 0.0, 0.0};
      UInt tapCount = 0;
      Dbl clickTimes[3] = {0.0, 0.0, 0.0};
      UInt clickCount = 0;
      Dbl lastClick = 0.0;
      bool clickSeen = false;
      bool clickArmed = true;

      Dbl tickTimes[TICK_HISTORY] = {};
      UInt tickHead = 0;
      UInt tickFill = 0;
      std::uint64_t tickIndex = 0;
      bool tickLocked = false;
      bool tickTempo = false;

      Int lastClockTick = -1;

      bool reportDone = false;
      UInt reportTaps = 0;
      UInt reportClicks = 0;
      UInt reportTicks = 0;
      bool reportTempo = false;
      Int clockOut[MAX_CLOCK_BYTES] = {};
      UInt clockCount = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_CLOCKFOLLOWER_HPP
