/*
  ==============================================================================

    player.hpp
    Created: 8 Apr 2015 5:16:24pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYERIMPLEMENTATION_H_INCLUDED
#define PLAYERIMPLEMENTATION_H_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../headers/enums.hpp"
#include "../utils/lfQueue.hpp"
#include "../utils/interpolators.hpp"
#include "../music/note.hpp"
#include "../music/pNote.hpp"
#include "../synth/synth.hpp" // SYNTH::interfaceObject — the synth the player drives (#156)
#include "../dsp/ramp.hpp"
#include <atomic>
#include <vector>

namespace YSE {
  namespace PLAYER {

    // Fixed ceilings for the preallocated audio-thread pools (issue #195). The
    // note generator runs on the audio callback and must not allocate, so every
    // container it touches is reserved to these bounds at construction time and
    // never grows past them: generation above the ceiling is dropped rather than
    // triggering a heap reallocation.
    static constexpr UInt MAX_VOICES = 32; // polyphony ceiling
    static constexpr UInt MAX_MOTIF_NOTES = 256; // notes copied per voice motif
    static constexpr UInt MAX_MOTIFS = 64; // weighted motifs in the pool
    static constexpr UInt MAX_NOTES = 512; // concurrently sounding notes

    class implementationObject {
    public:
      // Constructing an implementation preallocates every pool it uses on the
      // audio thread. `head` may be null (used by tests that drive update()
      // directly); when non-null the interface's pimpl is wired back here.
      // `instrument` is the implementation of the synth this player feeds notes
      // into (#156); null when a test drives generation in isolation with no
      // synth attached. The player holds a reference on it until it retires, so
      // it stays valid after the synth interface is gone (#1181).
      explicit implementationObject(player* head,
                                    SYNTH::implementationObject* instrument = nullptr);
      ~implementationObject();

      bool update(Flt delta);
      void parseMessage(const messageObject& message);

      inline void sendMessage(const messageObject& message) {
        messages.push(message);
      }

      void removeInterface();

      // ---- lifecycle (slow-pool delete job, mirrors the MIDI/synth managers) --
      // The manager retires an orphaned player by flagging it OBJECT_DELETE and
      // letting the slow-pool delete job reap it — the audio thread never frees
      // an impl (#156, consistent with #155 / #190).
      void setStatus(OBJECT_IMPLEMENTATION_STATE value) {
        objectStatus.store(value);
      }
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus.load() == OBJECT_DELETE;
      }

      // Read-only inspection of the sounding-note pool. Lets the RT-allocation
      // test assert the pool stays within its fixed ceiling (#195).
      UInt noteCount() const {
        return static_cast<UInt>(notes.size());
      }

    private:
      // Push a note on/off onto the synth's lock-free inbox — the same bounded
      // push the synth interface's noteOn()/noteOff() make (#1181).
      void sendNote(bool on, const MUSIC::note& note);

      std::atomic<player*> head;
      lfQueue<messageObject> messages;
      // The implementation of the synth this player feeds generated notes into.
      // Set at construction from player::create(synth&); null when generation
      // is driven in isolation (tests). Only touched on the audio thread in
      // update() (#156). It is the engine-owned impl, not the caller-owned
      // interface, and this player holds a reference on it (retainPlayer) until
      // the retirement tick has flushed its note-offs, so a synth destroyed
      // before that tick is never dereferenced (#1181).
      SYNTH::implementationObject* instrument;
      // Lifecycle state for the audio-thread / slow-pool delete handoff. Starts
      // OBJECT_READY (the player needs no async setup) and only moves to
      // OBJECT_DELETE once the audio thread has retired it from the manager's
      // working list (#156).
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;
      float waitTime;

      struct voice {
        voice(bool active)
          : isActive(active), notePlaying(false), motifPlaying(false), duration(0) {}
        Bool isActive;
        Bool notePlaying;
        Bool motifPlaying;
        Bool hasMotif;
        Flt duration;
        Flt motifTime;
        UInt motifPos;
        Flt motifVolume;
        std::vector<MUSIC::pNote> motif;
      };

      void setVoiceFromMotif(voice& v, Flt delta);

      std::vector<MUSIC::note> notes;
      std::vector<voice> voices;
      UInt activeVoices;

      bool playing;

      // modifiers
      linearInterpolator minimumPitch;
      linearInterpolator maximumPitch;
      linearInterpolator minimumVelocity;
      linearInterpolator maximumVelocity;
      linearInterpolator minGap; // gap between notes or motifs
      linearInterpolator maxGap;
      linearInterpolator minLength; // min note duration when no motif is supplied
      linearInterpolator maxLength;
      linearInterpolator numVoices; // simultanious voices
      linearInterpolator partialMotif;
      linearInterpolator playMotif;
      linearInterpolator motifToScale;
      objectInterpolator<YSE::SCALE::implementationObject*> scale;

      struct wMotif {
        YSE::MOTIF::implementationObject* motif;
        UInt weight;
      };

      std::vector<wMotif> motifs;

      friend class player;
      friend class PLAYER::managerObject;
    };

  } // namespace PLAYER
} // namespace YSE

#endif // PLAYER_HPP_INCLUDED
