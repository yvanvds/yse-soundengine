#include "stdafx.h"
#include "samplerImpl.h"
#include "internal/internalObjects.h"
/*
namespace YSE {
  namespace INSTRUMENTS {

    

    void samplerImpl::create(const char * fileName, Int voices, Int pitch, channel& parent) {
      // create enough sounds
      baseInstrumentImpl::create(voices, parent);

      // set standard pitch
      this->pitch = pitch;

      // load file into voice and add to channel
      FOR (this->voices) {
        this->voices[i].create(fileName, &this->ch);
      }

      ready = true;
    }

    void samplerImpl::updateVoices() {
      for (Int i = 0; i < maxVoices; i++) {
        if (vm[i].sigStop) {
          voices[i].stop(vm[i].latency);
          vm[i].sigStop = false;
          vm[i].active = false;
        }

        if (vm[i].sigRestart) {
          voices[i].volume(vm[i].velocity);
          voices[i].speed(getSpeed(vm[i].pitch));
          voices[i].restart(vm[i].latency);
          vm[i].sigRestart = false;
        }

        if (vm[i].sigPlay) {
          voices[i].volume(vm[i].velocity);
          voices[i].speed(getSpeed(vm[i].pitch));
          voices[i].play(vm[i].latency);
          vm[i].sigPlay = false;
        }
      }
    }

    Flt samplerImpl::getSpeed(Flt pitch) {
      return std::pow(2, (pitch - this->pitch) / 12.0f);
    }

  } // end INSTRUMENTS
}   // end YSE

*/