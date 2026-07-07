/*
  ==============================================================================

    synthInterface.hpp
    Public interface for the YSE::synth subsystem — a thin, chainable pimpl.

    Implements the #153 slice of §12 ("Public API surface") of
    docs/design/synth_core.md: object creation, voice-group construction, and
    note events. Keyboard state / pedals / controllers / onNoteEvent are the
    next issue (#154) and are intentionally absent here.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SYNTHINTERFACE_HPP
#define YSE_SYNTH_SYNTHINTERFACE_HPP

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../utils/vector.hpp" // Pos (notePosition / getVoicePosition)
#include "synth.hpp"

namespace YSE {

  namespace SYNTH {

    /**
     *  @brief A polyphonic synthesiser voice pool rendered behind one ``YSE::sound``.
     *
     *  Write ``YSE::synth`` (a typedef for this class). Build the pool from a
     *  prototype voice with ``addVoices``, attach it behind a positioned
     *  ``YSE::sound`` with ``sound::create(synth&, ...)``, then drive it with
     *  ``noteOn`` / ``noteOff``. The engine owns polyphony, allocation, voice
     *  stealing and lifecycle; a ``SYNTH::dspVoice`` subclass owns only what a
     *  single voice sounds like.
     *
     *  Like ``YSE::sound`` the interface is **non-copyable**: the implementation
     *  holds this object's address, so the address must stay stable.
     *
     *  @see YSE::SYNTH::dspVoice   For user-subclassable voices.
     *  @see YSE::SYNTH::sineVoice  The reference sine + ADSR voice.
     */
    class API interfaceObject {
    public:
      interfaceObject();
      ~interfaceObject();

      /** @brief Synths are non-copyable (the implementation holds our address). */
      interfaceObject(const interfaceObject&) = delete;
      interfaceObject& operator=(const interfaceObject&) = delete;

      /** @brief Register this synth with the engine.
       *
       *  Must be called before ``addVoices`` / ``noteOn``. Idempotent-safe to
       *  omit: ``sound::create(synth&, ...)`` calls it for you if you have not.
       */
      interfaceObject& create();

      /**
       *  @brief Clone ``prototype`` ``numVoices`` times into a voice group.
       *
       *  The group responds to note numbers in ``[lowestNote, highestNote]`` on
       *  MIDI ``channel`` (0 = omni). May be called several times to build
       *  layered or split keyboards; a note may sound in more than one matching
       *  group. Cloning happens off the audio thread, on the engine's setup
       *  pool, so the synth becomes playable a short moment after this returns
       *  (when it reaches ``OBJECT_READY``) — exactly like a file-backed sound
       *  is not playable until its buffer finishes loading.
       *
       *  @warning ``prototype`` must outlive the resulting setup — the engine
       *           reads it to clone but neither copies nor owns it. Call
       *           ``addVoices`` before the synth is attached and played.
       */
      interfaceObject& addVoices(dspVoice& prototype, int numVoices, int channel = 0,
                                 int lowestNote = 0, int highestNote = 127);

      /** @brief Start a note. ``velocity`` is normalised to [0, 1]. */
      interfaceObject& noteOn(int channel, int noteNumber, float velocity);

      /** @brief Release a note. */
      interfaceObject& noteOff(int channel, int noteNumber, float velocity = 0.f);

      /** @brief Start a note from a ``MUSIC::note`` (uses its pitch/volume/channel). */
      interfaceObject& noteOn(const MUSIC::note& note);

      /** @brief Release the note matching a ``MUSIC::note`` (pitch/channel). */
      interfaceObject& noteOff(const MUSIC::note& note);

      /** @brief Release every held note on ``channel`` (0 = all channels).
       *
       *  A bulk note-off: voices enter their normal release, they are not cut. */
      interfaceObject& allNotesOff(int channel = 0);

      // ---- keyboard state, pedals and controllers (§5) ----------------------

      /** @brief Bend every voice on ``channel``. ``value`` is normalised to
       *  [-1, 1] (0 = centre). How far a voice bends is the voice's concern. */
      interfaceObject& pitchWheel(int channel, float value);

      /** @brief Send a control-change. ``value`` is normalised to [0, 1].
       *
       *  CC 64 / 66 / 67 act as the sustain / sostenuto / soft pedals; every
       *  other CC number is stored as the channel's last controller value. */
      interfaceObject& controller(int channel, int number, float value);

      /** @brief Apply aftertouch pressure, normalised to [0, 1].
       *
       *  ``noteNumber == -1`` is channel-wide (every voice on the channel);
       *  otherwise only the voice(s) sounding that note receive it. */
      interfaceObject& aftertouch(int channel, int noteNumber, float value);

      /** @brief Sustain pedal (CC 64). Down defers NOTE_OFF releases on the
       *  channel; up releases every note that was waiting on it. */
      interfaceObject& sustain(int channel, bool down);

      /** @brief Sostenuto pedal (CC 66). Down captures the currently-held notes
       *  and sustains only those; up releases them. */
      interfaceObject& sostenuto(int channel, bool down);

      /** @brief Soft pedal (CC 67). While down, notes that START scale their
       *  velocity down; sounding voices are unaffected. */
      interfaceObject& softPedal(int channel, bool down);

      // ---- per-note 3D positioning (§14, issue #170) -----------------------

      /** @brief Clone ``prototype`` once per voice slot as this synth's position
       *  handler, giving every note its own 3D position and movement.
       *
       *  With no handler attached, every voice uses the synth's aggregate
       *  position — so this call is purely additive. Ship-in handlers are
       *  ``SYNTH::staticHandler`` / ``SYNTH::randomSpreadHandler`` /
       *  ``SYNTH::orbitHandler``, or derive your own from
       *  ``SYNTH::positionHandler``.
       *
       *  @warning ``prototype`` must outlive setup — the engine reads it to
       *           clone but neither copies nor owns it. Call before the synth is
       *           attached and played (rejected after setup, like ``addVoices``). */
      interfaceObject& positionHandler(YSE::SYNTH::positionHandler& prototype);

      /** @brief Set a shared handler parameter by index (e.g. the swarm centre
       *  at indices 0..2). All of the synth's live handlers read it next block.
       *  A bounded, allocation-free message — safe to call every control tick. */
      interfaceObject& handlerParam(int index, float value);

      /** @brief Imperatively place the voice(s) sounding ``noteNumber`` on
       *  ``channel`` at ``pos`` (app-driven trajectories). A bounded,
       *  allocation-free message. When a handler is attached it re-steers the
       *  voice next block, so this is primarily for the no-handler case. */
      interfaceObject& notePosition(int channel, int noteNumber, const Pos& pos);

      /** @brief Current position of a voice sounding (``channel``, ``noteNumber``),
       *  or the origin if none is. Best-effort audio-thread snapshot for
       *  tests / metering. */
      Pos getVoicePosition(int channel, int noteNumber) const;

      /** @brief Install an audio-thread note-rewrite hook, or clear it with
       *  ``nullptr``.
       *
       *  ``func`` runs on the audio thread for every NOTE_ON / NOTE_OFF, before
       *  keyboard bookkeeping and allocation, and may rewrite ``*noteNumber``
       *  and ``*velocity`` in place — the classic use is transposition,
       *  retuning, velocity curves or note filtering. Only a free function or
       *  captureless lambda is accepted (it carries no heap closure). It must
       *  obey the same real-time rules as a voice ``process()``: no allocation,
       *  no locks, no blocking. See docs/design/synth_core.md §7. */
      interfaceObject& onNoteEvent(void (*func)(bool noteOn, float* noteNumber, float* velocity));

      /** @brief Total number of allocated voices across all groups. */
      int getNumVoices() const;

      /** @brief Whether this interface has a live implementation. */
      bool isValid() const;

    private:
      implementationObject* pimpl = nullptr;

      friend class YSE::sound; // sound::create(synth&, ...)
      friend class SYNTH::implementationObject; // impl holds our address
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SYNTHINTERFACE_HPP
