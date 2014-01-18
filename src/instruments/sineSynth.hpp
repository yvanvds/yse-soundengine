#pragma once
#include "channel.hpp"
#include "instruments\instrument.hpp"

namespace YSE {

  namespace INSTRUMENTS {
    class sineSynthImpl; // for internal use

//======================================================================================
/* sampler is a very basic virtual instrument that can be added to a track.
   Virtual instruments make it easy to play sounds by defining pitch, 
   velocity and length, like when working with a midi program.

   You can pass only one sound to this object, but it can be played at several
   pitches. Take care of the voices parameter when creating this instrument. Every
   voice will take additional processing power (although not that much). You can never
   play more notes at the same time than there are voices. If additional notes are 
   asked to be played, Sampler will stop the note with the lowest velocity.

   Remember that virtual instruments are meant to be controlled from within tracks!
*/
//======================================================================================

    class API sineSynth : public instrument {
    public:
      sineSynth& create(Int voices); // filename of your sample, max nr of simultanious voices, normal pitch of your sample (sample will sound at normal speed if this note is played), channel is Global channel
      sineSynth& create(Int voices, channel & parent); // manually choose a channel to add this instrument to

//======================================================================================
      // parts below are not important for use
      sineSynth();
     ~sineSynth();
    private:
      sineSynthImpl * impl;
      friend class YSE::MUSIC::track;
    };
  }
}