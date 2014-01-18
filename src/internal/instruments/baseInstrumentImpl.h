#pragma once
#include <boost/ptr_container/ptr_vector.hpp>
#include "channel.hpp"
#include "sound.hpp"
#include "music/chord.hpp"

/*
namespace YSE {
  namespace INSTRUMENTS {

    // voicemap is used because there's no way to stop or fade out old sounds in an elegant way if we
    // work on sounds directly. (It would also cause timing problems.)
    struct voiceMap {
      aBool active;
      aFlt pitch;
      aFlt velocity;
      aInt length;
      aUInt latency;
      aBool sigStop;
      aBool sigRestart;
      aBool sigPlay;
      voiceMap() : active(false), pitch(0.0f), velocity(0.0f), length(-1), 
        sigStop(false), sigRestart(false), sigPlay(false) {}
      void stop   (UInt l) { sigStop = true ; sigRestart = false; sigPlay = false; latency = l; }
      void restart(UInt l) { sigStop = false; sigRestart = true ; sigPlay = false; latency = l; }
      void play   (UInt l) { sigStop = false; sigRestart = false; sigPlay = true ; latency = l; }
    };

    // base class for virtual instrument implementations
    // cannot be used directly
    // choose sampler or wavesynth instead

    class baseInstrumentImpl {
    public:
      // for voices
      void create(Int value, channel& parent); // create synth with 'value' voices, can only be called once
      Int  size  (); // return nr of voices in this synth
      boost::ptr_vector<sound> voices;
      voiceMap * vm;
      Int maxVoices;
      
      // all voices of an instrument are attached to a channel
      channel ch;

      Int lowestNote;
      Int highestNote;
      Bool ready; // set to true after create
      Int nextVoice;

      void findFreeVoice();
      void play(Flt pitch, Flt velocity, Int length = -1);
      void stop(Flt pitch);
      void allNotesOff();
      void update(); // called from timer thread
      virtual void updateVoices() = 0;

      baseInstrumentImpl();
     ~baseInstrumentImpl();

    private:
      
    };
  }
}
*/