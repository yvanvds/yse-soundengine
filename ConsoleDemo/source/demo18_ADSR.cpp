#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"

#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

Bool tableReady = false;

class synthVoice : public YSE::SYNTH::dspVoice {
public:

  synthVoice() {
    generator.initialize(getTable());
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.f, 0.f, 0.2));
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.1f, 1.f, 4));
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.2f, 0.5f, 2, true));
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.3f, 0.9f, 0.5));
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.4f, 0.5f, 0.5, false, true));
    envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.5f, 0.f, 0.5));
    envelope.generate();
  }

  virtual dspVoice * clone() {
    return new synthVoice();
  }

  virtual void process(YSE::SOUND_STATUS & intent) {
    YSE::DSP::ADSRenvelope::STATE state = YSE::DSP::ADSRenvelope::RESUME;

    if (intent == YSE::SS_WANTSTOPLAY) {
      state = YSE::DSP::ADSRenvelope::ATTACK;
      intent = YSE::SS_PLAYING;
      releaseRequested = false;
    }
    else if (intent == YSE::SS_WANTSTOSTOP && !releaseRequested) {
      state = YSE::DSP::ADSRenvelope::RELEASE;
      releaseRequested = true;
    }

    
    out = generator(getFrequency());
    out *= envelope(state);
    out *= 0.25f;
    
    if (envelope.isAtEnd()) intent = YSE::SS_STOPPED;

    for (UInt i = 0; i < buffer.size(); i++) {
      buffer[i] = out;
    }
    
  }

private:
  YSE::DSP::oscillator generator;
  YSE::DSP::ADSRenvelope envelope;
  YSE::DSP::buffer out;
  bool releaseRequested;

  static YSE::DSP::wavetable & getTable() {
    static YSE::DSP::wavetable  table;
    if (!tableReady) {
      table.createTriangle(8, 1024);
      tableReady = true;
    }
    return table;
  }

};

YSE::DSP::ADSRenvelope envelope;

YSE::synth synth;
YSE::sound sound;

Int bassNote, middleNote;

int main() {
  YSE::System().init();


  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.f, 0.f, 0.2));
  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.1f, 1.f, 4));
  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.2f, 0.5f, 0.5, true));
  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.5f, 0.6f, 0.5));
  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(0.9f, 0.5f, 0.5, false, true));
  envelope.addPoint(YSE::DSP::ADSRenvelope::breakPoint(1.0f, 0.f, 0.5));
  envelope.generate();
  envelope.saveToFile("ADSRtest");
  

  std::cout << "press e to exit." << std::endl;

  synth.create();
  synthVoice voice;
  synth.addVoices(&voice, 8, 1);
  sound.create(synth).play();

  int counter = 0;
  while (true) {

    // random player
    if (YSE::Random(10) == 0) {
      synth.noteOff(1, bassNote);
      bassNote = YSE::Random(30, 50);
      Flt vel = YSE::RandomF(0.8, 0.9);
      synth.noteOn(1, bassNote, vel);
    }

    // melody
    if (YSE::Random(6) == 0) {
      synth.noteOff(1, middleNote);
      synth.noteOff(1, middleNote - 3);
      middleNote = YSE::Random(55, 65);
      synth.noteOn(1, middleNote, YSE::RandomF(0.5, 0.7));
      synth.noteOn(1, middleNote - 3, YSE::RandomF(0.5, 0.7));
    }

    /*if (counter % 20 == 0) {
      synth.noteOn(1, 60, 0.3);
      synth.noteOn(1, 67, 0.3);
      synth.noteOff(1, 62);
      synth.noteOff(1, 70);
    }
    else if (counter % 10 == 0) {
      synth.noteOff(1, 60);
      synth.noteOff(1, 67);
      synth.noteOn(1, 62, 0.3);
      synth.noteOn(1, 70, 0.3);
    }*/

    counter++;

    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {


      case 'e': goto exit;
      }
    }
    YSE::System().sleep(100);
    YSE::System().update();
  }


exit:
  YSE::System().close();
  return 0;
}