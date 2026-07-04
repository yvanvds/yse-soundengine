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
#include "../utils/lfQueue.hpp"
#include "../utils/interpolators.hpp"
#include "../music/note.hpp"
#include "../music/pNote.hpp"
#include "../dsp/ramp.hpp"
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
      explicit implementationObject(player* head);
      ~implementationObject();

      bool update(Flt delta);
      void parseMessage(const messageObject& message);

      inline void sendMessage(const messageObject& message) {
        messages.push(message);
      }

      void removeInterface();

      // Read-only inspection of the sounding-note pool. Lets the RT-allocation
      // test assert the pool stays within its fixed ceiling (#195).
      UInt noteCount() const {
        return static_cast<UInt>(notes.size());
      }

    private:
      std::atomic<player*> head;
      lfQueue<messageObject> messages;
      // synth * instrument;
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
