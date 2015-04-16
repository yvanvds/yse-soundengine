#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif


class SineWaveVoice : public YSE::SYNTH::dspVoice {
public:

  SineWaveVoice() {
    ramp.set(0);
    ramp.update();
  }

  virtual dspVoice * clone() {
    return new SineWaveVoice();
  }

  virtual void process(YSE::SOUND_STATUS & intent) {
    assert(intent != YSE::SS_STOPPED);
    if (intent == YSE::SS_WANTSTOPLAY) {     
      ramp.set(1, 1);
      intent = YSE::SS_PLAYING;

    } else if (intent == YSE::SS_WANTSTOSTOP) {
      ramp.setIfNew(0, 1);
    }

    // generate tone with frequency and velocity
    out = generator(getFrequency());
    out *= getVelocity() * 0.25f; 
      
    ramp.update();
      
    if (intent == YSE::SS_WANTSTOSTOP && ramp.getValue() <= 0) {
      intent = YSE::SS_STOPPED;
    }

    out *= ramp;
      

    
    // copy buffer to all channels (YSE creates the buffer vector for your dsp, according to 
    // the channels chosen for the current output device
    for (UInt i = 0; i < buffer.size(); i++) {
      buffer[i] = out;
    }
  }

private:
  YSE::DSP::sine generator;
  YSE::DSP::sample out;
  YSE::DSP::ramp ramp;
};

YSE::SYNTH::samplerConfig demo;
YSE::sound sound;
YSE::synth synth;


Int bassNote;
Int middleNote;
Int highNote;

int main() {
  

  YSE::System().init();

  demo.name("demo").file("c.wav").channel(1).root(72);
  synth.create();
  synth.addVoices(demo,4);
  sound.create(synth).play();

  // We need a SineWaveVoice object here to pass it to the function, but it won't
  // actually be used itself. The engine creates copies of this object, depending 
  // on the number of voices you specify. Do not try to access an object derived 
  // from dspVoice yourself.
  {
    SineWaveVoice voice;
    synth.addVoices(&voice, 16, 2); // 16 voices on channel 2
  }

  int counter = 0;
  while (true) {
    if (counter > 5) {
      // base
      if (YSE::Random(10) == 0) {
        synth.noteOff(2, bassNote);
        bassNote = YSE::Random(40, 60);
        Flt vel = YSE::RandomF(0.8, 0.9);
        synth.noteOn(2, bassNote, vel);
      }

      // melody
      if (YSE::Random(6) == 0) {
        synth.noteOff(1, middleNote);
        synth.noteOff(1, middleNote - 3);
        middleNote = YSE::Random(55, 65);
        synth.noteOn(1, middleNote, YSE::RandomF(0.5, 0.7));
        synth.noteOn(1, middleNote - 3, YSE::RandomF(0.5, 0.7));
      }

      // countermelody
      if (YSE::Random(3) == 0) {
        synth.noteOff(2, highNote);
        highNote = YSE::Random(80, 100);
        synth.noteOn(2, highNote, YSE::RandomF(0.2, 0.3));
      }

      // chords
      if (counter % 20 == 0) {
        synth.noteOn(2, 60, 0.3);
        synth.noteOn(2, 67, 0.3);
        synth.noteOff(2, 62);
        synth.noteOff(2, 70);
      }
      else if (counter % 10 == 0) {
        synth.noteOff(2, 60);
        synth.noteOff(2, 67);
        synth.noteOn(2, 62, 0.3);
        synth.noteOn(2, 70, 0.3);
      }
      

      if (_kbhit()) {
        char ch = _getch();
        YSE::Vec pos = YSE::Listener().getPosition();
        switch (ch) {
        case 'e': goto exit;
        }
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();
    counter++;
  }

exit:
  YSE::System().close();
  return 0;
}