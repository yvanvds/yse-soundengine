#include "stdafx.h"
#include "baseInstrumentImpl.h"
#include "internal/internalObjects.h"
#include "internal/music/globalTrack.h"
/*
namespace YSE {
  namespace INSTRUMENTS {
    baseInstrumentImpl::baseInstrumentImpl() : 
      lowestNote(0),
      highestNote(127),
      ready(false),
      maxVoices(0),
      nextVoice(0)
    {
      lock l(MTX());
      Instruments().push_back(this);
    }

    baseInstrumentImpl::~baseInstrumentImpl() {
      lock l(MTX());
      if (maxVoices > 0) {
        delete[] vm;
      }
      std::vector<INSTRUMENTS::baseInstrumentImpl*>::iterator i = Instruments().begin();
      while (i != Instruments().end()) {
        if (*i == this) {
          Instruments().erase(i);
          break;
        }
      }
    }
  
    void baseInstrumentImpl::create(Int value, channel& parent) {
      if (ready) return;
      // should not be needed, but to be sure...
      if (maxVoices > 0) delete[] vm;
      vm = new voiceMap[value];
      maxVoices = value;
      voices.resize(value);
      ch.create(parent);
      ready = true;
    }

    Int baseInstrumentImpl::size() {
      return maxVoices;
    }

    void baseInstrumentImpl::update() {
      if (!ready) return;
      for (Int i = 0; i < maxVoices; i++) {
        if (vm[i].active && vm[i].length > -1) {
          vm[i].length--;
          if (vm[i].length < 1) {
            vm[i].stop(MUSIC::latency);
          }
        }
      }
    }

    void baseInstrumentImpl::findFreeVoice() {
      // first selection
      nextVoice++;
      if(nextVoice == maxVoices) nextVoice = 0;

      // but if in use find another one
      if (vm[nextVoice].active) {
        for (Int i = 0; i < maxVoices; i++) {
          if (!vm[i].active) {
            nextVoice = i;
            break;
          }
        }
      }

      // if all in use, take sound with lowest velocity
      if (vm[nextVoice].active) {
        Flt lowestVelocity = 1.0f;
        for (Int i = 0; i < maxVoices; i++) {
          if (vm[i].velocity < lowestVelocity) {
            nextVoice = i;
            lowestVelocity = vm[i].velocity;
          }
        }
      }
    }

    void baseInstrumentImpl::play(Flt pitch, Flt velocity, Int length) {
      if (!ready) return;
      // check if pitch is played already
      for (Int i = 0; i < maxVoices; i++) {
        if (vm[i].active && vm[i].pitch == pitch) {
          // note is already playing, restart and change velocity
          vm[i].length = length;
          vm[i].velocity = velocity;
          vm[i].restart(MUSIC::latency);
          return;
        }
      }

      // start new voice if we get here
      findFreeVoice();
    
      vm[nextVoice].pitch = pitch;
      vm[nextVoice].length = length;
      vm[nextVoice].velocity = velocity;
      if (vm[nextVoice].active) {
        vm[nextVoice].restart(MUSIC::latency);
      } else {
        vm[nextVoice].play(MUSIC::latency);
        vm[nextVoice].active = true;
      }
    }

    void baseInstrumentImpl::stop(Flt pitch) {
      if (!ready) return;
      for (Int i = 0; i < maxVoices; i++) {
        if(vm[i].pitch == pitch) {
          vm[i].stop(MUSIC::latency);
          return;
        }
      }
    }

    void baseInstrumentImpl::allNotesOff() {
      FOR(voices) {
        vm[i].stop(MUSIC::latency);
      }
    }

  } // end INSTRUMENTS
}   // end YSE
*/
