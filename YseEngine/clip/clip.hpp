/*
  ==============================================================================

    clip.hpp
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#ifndef YSE_CLIP_CLIP_HPP
#define YSE_CLIP_CLIP_HPP

#include <string>
#include <vector>

#include "../headers/defines.hpp"
#include "../synth/synth.hpp" // YSE::synth (for connect/disconnect)

namespace YSE {

  /**
   *  @brief One timed note event in a clip, positioned in *beats*.
   *
   *  Times are beat offsets on the clip's bound domain clock, not absolute
   *  seconds — the transport evaluates them against the clock every audio
   *  block, so a tempo change on the clock bends the clip immediately with no
   *  rescheduling. ``startBeat`` is taken modulo the clip's loop length, so an
   *  event repeats every loop.
   */
  struct clipEvent {
    /** Beat within the loop at which the note starts (>= 0). */
    double startBeat = 0.0;
    /** Note length in beats. The note-off fires ``durationBeats`` after each
        note-on, independent of the event list — so a note still sounding when
        the list is swapped is released on time even if it vanished from the new
        list. */
    double durationBeats = 0.0;
    /** MIDI channel, 1..16. */
    int channel = 1;
    /** MIDI note number, 0..127. */
    int pitch = 60;
    /** Velocity, normalized to [0, 1]. */
    float velocity = 0.8f;
    /** Optional per-note pitch bend, normalized to [-1, 1] (0 = none). Emitted
        as a channel pitch-wheel just before the note-on — Phi voices fractional
        / microtonal pitch as nearest semitone + bend. */
    float pitchBend = 0.0f;
  };

  /// @cond INTERNAL
  namespace CLIP {
    class transport;
  }
  /// @endcond

  /**
   *  @brief A looping clip transport: plays a timed note-event list against a
   *         domain clock, dispatched from the audio thread.
   *
   *  The clip binds to a named domain clock (see ``System().createClock``). It
   *  owns an immutable list of ``clipEvent`` and a loop length in beats. Every
   *  audio block it converts that block's boundaries into a beat window on the
   *  clock and fires exactly the events whose crossings fall inside it — so the
   *  UI thread never dispatches a note, and tempo changes on the clock bend the
   *  clip with no rescheduling.
   *
   *  The event list is replaceable while playing: ``setEvents`` publishes a new
   *  list that the transport swaps in at the next block boundary, lock-free with
   *  respect to the audio thread. Sounding-note bookkeeping survives the swap —
   *  a note that vanished from the new list still gets its note-off on time.
   *
   *  Output currently targets one or more ``YSE::synth`` instances (block-
   *  accurate, on the audio thread), the RT-safe internal event-queue target;
   *  the transport itself is agnostic to the sink, so an external MIDI-out sink
   *  can be added on the same seam.
   *
   *  Multiple clips run concurrently, each on its own clock. Non-copyable /
   *  non-movable — wrap in a ``unique_ptr`` if you need transferable ownership.
   */
  class API clip {
  public:
    clip();
    ~clip();

    clip(const clip&) = delete;
    clip& operator=(const clip&) = delete;
    clip(clip&&) = delete;
    clip& operator=(clip&&) = delete;

    /** @brief Bind the clip to a domain clock by name.
     *
     *  @param clockName Name of a live domain clock (``System().createClock``).
     *  @return ``true`` on success; ``false`` if no live clock owns the name.
     *
     *  The bound clock must outlive the clip (destroy the clip before the
     *  clock). Control thread only. */
    bool create(const std::string& clockName);

    /** @brief Replace the event list. Takes effect at the next block boundary,
     *         lock-free with respect to the audio thread. Control thread only. */
    clip& setEvents(const std::vector<clipEvent>& events);

    /** @brief Set the loop length in beats. Values <= 0 disable looping (events
     *         fire once). Control thread only. */
    clip& loopLength(double beats);

    /** @brief Route this clip's playback into a synth. Every note / pitch-bend
     *         it fires is delivered to ``synth`` block-accurately on the audio
     *         thread. May be called for several synths.
     *
     *  @warning ``synth`` must outlive the connection: ``disconnect`` it (or
     *           destroy this clip) before destroying the synth. Control thread
     *           only. */
    clip& connect(YSE::synth& synth);

    /** @brief Stop routing this clip's playback into ``synth``. */
    clip& disconnect(YSE::synth& synth);

    /** @brief Start (or restart) playback from the clock's current beat. */
    clip& play();

    /** @brief Stop playback; emits note-offs for everything currently sounding. */
    clip& stop();

    /** @brief Whether the clip is currently playing. */
    bool isPlaying() const;

  private:
    CLIP::transport* pimpl;
  };

} // namespace YSE

#endif // YSE_CLIP_CLIP_HPP
