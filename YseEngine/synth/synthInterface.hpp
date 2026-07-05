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
