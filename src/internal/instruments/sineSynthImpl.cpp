#include "stdafx.h"
#include "sineSynthImpl.h"
#include "dsp/math.hpp"
/*
namespace YSE {
  namespace INSTRUMENTS {

    void sineSynthImpl::create(Int voices, channel& parent) {
      baseInstrumentImpl::create(voices, parent);

      sources.resize(this->voices.size());
      FOR(this->voices) {
        this->voices[i].create(sources[i], &this->ch);
      }

      ready = true;
    }

    void sineSynthImpl::updateVoices() {
      for (Int i = 0; i < maxVoices; i++) {
        if (vm[i].sigStop) {
          voices[i].stop(vm[i].latency);
          vm[i].sigStop = false;
          vm[i].active = false;
        }

        if (vm[i].sigRestart) {
          voices[i].volume(vm[i].velocity);
          voices[i].speed(DSP::MidiToFreq(vm[i].pitch));
          voices[i].restart(vm[i].latency);
          vm[i].sigRestart = false;
        }

        if (vm[i].sigPlay) {
          voices[i].volume(vm[i].velocity);
          voices[i].speed(DSP::MidiToFreq(vm[i].pitch));
          voices[i].play(vm[i].latency);
          vm[i].sigPlay = false;
        }
      }
    }


  } // end INSTRUMENTS
}   // end YSE

*/