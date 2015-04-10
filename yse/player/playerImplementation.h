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
#include "../dsp/ramp.hpp"
#include <deque>

namespace YSE {
  namespace PLAYER {

    class implementationObject {
    public:
      implementationObject(interfaceObject * head, synth * s);
      ~implementationObject();

      bool update(Flt delta);
      void parseMessage(const messageObject & message);

      inline void sendMessage(const messageObject & message) {
        messages.push(message);
      }

      void removeInterface();

    private:
      std::atomic<interfaceObject *> head;
      lfQueue<messageObject> messages;
      synth * instrument;
      float waitTime;
      
      struct voice {
        voice(bool active) : isActive(active), notePlaying(false), duration(0) {}
        Bool isActive;
        Bool notePlaying;
        Flt duration;
      };
      
      std::deque<MUSIC::note> notes;
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
      objectInterpolator<MUSIC::scale> scale;

      friend class PLAYER::interfaceObject;
      friend class PLAYER::managerObject;
    };

  }
}



#endif  // PLAYER_HPP_INCLUDED
