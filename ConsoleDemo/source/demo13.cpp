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
      ramp.set(1, 10);
      intent = YSE::SS_PLAYING;

    }
    else if (intent == YSE::SS_WANTSTOSTOP) {
      ramp.setIfNew(0, 200);
    }

    // generate tone with frequency and velocity
    out = generator[2](getFrequency()*3);
    out *= 0.3;
    out += generator[1](getFrequency() * 2);
    out *= 0.6;
    out += generator[0](getFrequency());

    out *= getVelocity() * 0.1f;
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
  YSE::DSP::sine generator[3];
  YSE::DSP::sample out;
  YSE::DSP::ramp ramp;

};


YSE::SYNTH::samplerConfig demo;
YSE::sound sound;
YSE::synth synth;
YSE::MIDI::file midiFile;
YSE::Vec soundPos;

int main() {
  YSE::System().init();

  synth.create();
  {
    SineWaveVoice voice;
    synth.addVoices(&voice, 16, 1);
  }
  demo.name("demo").file("c.wav").channel(1).root(72);
  //synth.addVoices(demo, 16);
  sound.create(synth).play();
  soundPos.set(5.f, 0.f, 1.f);
  sound.setPosition(soundPos);

  midiFile.create("demo2.mid");
  midiFile.connect(&synth);

  std::cout << "1: start midi file" << std::endl;
  std::cout << "2: pause midi file" << std::endl;
  std::cout << "3: stop  midi file" << std::endl;
  std::cout << "4/5: move sound position to left/right" << std::endl;
  Int counter = 0;
  while (true) {
    /*if (counter % 20 == 0) {
      synth.noteOn(1, 67, 0.7);
      synth.noteOn(1, 59, 0.7);
    }
    else if (counter % 10 == 0) {
      synth.noteOff(1, 67);
      synth.noteOff(1, 59);
    }
    counter++;
    */
    if (_kbhit()) {
      char ch = _getch();
      YSE::Vec pos = YSE::Listener().getPosition();
      switch (ch) {
      case '1': midiFile.play(); break;
      case '2': midiFile.pause(); break;
      case '3': midiFile.stop(); break;
      case '4': soundPos.x -= 0.1; sound.setPosition(soundPos); break;
      case '5': soundPos.x += 0.1; sound.setPosition(soundPos); break;
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