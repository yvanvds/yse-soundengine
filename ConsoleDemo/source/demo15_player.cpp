#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/*
  A player object can be used to play a synth. While synths can also be played
  directly or by supplying it with a midi file, a player object uses an algorithmic
  approach. It plays by itself, but you can alter how it plays directly or over 
  time. (The player class is considered 'proof of concept' and will be much more
  flexible in a future release.)
*/

// This class is a DSP source for the synth used in this tutorial. (It is 
// the same as the one in the previous tutorials.)

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
   // assert(intent != YSE::SS_STOPPED);
    if (intent == YSE::SS_WANTSTOPLAY) {
      ramp.set(1, 10);
      intent = YSE::SS_PLAYING;

    }
    else if (intent == YSE::SS_WANTSTOSTOP) {
      ramp.setIfNew(0, 200);
    }

    // generate tone with frequency and velocity
    out = generator[2](getFrequency() * 3);
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
    for (UInt i = 0; i < samples.size(); i++) {
      samples[i] = out;
    }
  }

private:
  YSE::DSP::sine generator[3];
  YSE::DSP::buffer out;
  YSE::DSP::ramp ramp;

};

// The yse objects used in this tutorial
YSE::sound sound; 
YSE::synth synth;
YSE::player player;
YSE::Vec soundPos;
YSE::scale scaleOne;
YSE::scale scaleTwo;

// values we need to remember
Flt minimumPitch = 40.f;
Flt maximumPitch = 60.f;
Flt minimumVelocity = 0.5f;
Flt maximumVelocity = 0.6f;
Flt minimumGap = 0.1f;
Flt maximumGap = 0.5f;
Flt minimumLength = 0.1f;
Flt maximumLength = 0.5f;
Flt numVoices = 1;

int main() {
  YSE::System().init();

  // create a synth with 16 voices
  synth.create();
  SineWaveVoice voice;
  synth.addVoices(&voice, 16, 1);
  
  // use this synth as a sound source
  sound.create(synth).play();
  soundPos.set(5.f, 0.f, 1.f);
  sound.setPosition(soundPos);

  // create a player which uses the synth, and set some starting values
  player.create(synth);
  player.setMinimumPitch(minimumPitch).setMaximumPitch(maximumPitch);
  player.setMinimumVelocity(minimumVelocity).setMaximumVelocity(maximumVelocity);
  player.setMinimumGap(minimumGap).setMaximumGap(maximumGap);
  player.setMinimumLength(minimumLength).setMaximumLength(maximumLength);
  player.setVoices(numVoices);

  // create two scales which can be used by a synth
  scaleOne.add(YSE::NOTE::C4)
    .add(YSE::NOTE::D4)
    .add(YSE::NOTE::E4)
    .add(YSE::NOTE::G4)
    .add(YSE::NOTE::A4);

  scaleTwo.add(YSE::NOTE::CS4)
    .add(YSE::NOTE::DS4)
    .add(YSE::NOTE::FS4)
    .add(YSE::NOTE::GS4)
    .add(YSE::NOTE::AS4);

  // start whith playing scale two
  player.setScale(scaleTwo);

  // the interface
  std::cout << "This demo demonstrates the player class, which can be used to play a synth. Not by supplying notes, but with changing parameters." << std::endl;
  std::cout << "1  : start player" << std::endl;
  std::cout << "2  : stop player" << std::endl;
  std::cout << "3-4: decrease/increase velocity over 5 seconds" << std::endl;
  std::cout << "5-6: lower/higher pitches over 5 seconds" << std::endl;
  std::cout << "7-8: shorter/longer pauses over 10 seconds" << std::endl;
  std::cout << "q-w: shorter/longer notes over 10 seconds" << std::endl;
  std::cout << "e-r: few/more voices over 5 seconds" << std::endl;
  std::cout << "t  : switch to C Pentatonic in 10 seconds" << std::endl;
  std::cout << "y  : switch to F# Pentatonic instantly" << std::endl;
  std::cout << "0: exit" << std::endl;

  Int counter = 0;
  while (true) {

    if (_kbhit()) {
      char ch = _getch();
      YSE::Vec pos = YSE::Listener().getPosition();
      switch (ch) {
      case '1': player.play(); break;
      case '2': player.stop(); break;
      case '3': {
                  minimumVelocity -= 0.1f;
                  maximumVelocity -= 0.1f;
                  player.setMinimumVelocity(minimumVelocity, 5.f).setMaximumVelocity(maximumVelocity, 5.f);
                  break;
      }

      case '4': {
                  minimumVelocity += 0.1f;
                  maximumVelocity += 0.1f;
                  player.setMinimumVelocity(minimumVelocity, 5.f).setMaximumVelocity(maximumVelocity, 5.f);
                  break;
      }

      case '5': {
                  minimumPitch -= 10.f;
                  maximumPitch -= 10.f;
                  player.setMinimumPitch(minimumPitch, 5.f).setMaximumPitch(maximumPitch, 5.f);
                  break;
      }

      case '6': {
                  minimumPitch += 10.f;
                  maximumPitch += 10.f;
                  player.setMinimumPitch(minimumPitch, 5.f).setMaximumPitch(maximumPitch, 5.f);
                  break;
      }

      case '7': {
                  minimumGap -= 0.5f;
                  maximumGap -= 0.5f;
                  player.setMinimumGap(minimumGap, 10.f).setMaximumGap(maximumGap, 10.f);
                  break;
      }

      case '8': {
                  minimumGap += 0.5f;
                  maximumGap += 0.5f;
                  player.setMinimumGap(minimumGap, 10.f).setMaximumGap(maximumGap, 10.f);
                  break;
      }

      case 'q': {
                  minimumLength -= 0.5f;
                  maximumLength -= 0.5f;
                  player.setMinimumLength(minimumLength, 10.f).setMaximumLength(maximumLength, 10.f);
                  break;
      }

      case 'w': {
                  minimumLength += 0.5f;
                  maximumLength += 0.5f;
                  player.setMinimumLength(minimumLength, 10.f).setMaximumLength(maximumLength, 10.f);
                  break;
      }

      case 'e': {
                  numVoices -= 5;
                  player.setVoices(numVoices, 5.f);
                  break;
      }

      case 'r': {
                  numVoices += 5;
                  player.setVoices(numVoices, 5.f);
                  break;
      }

      case 't': {
                  player.setScale(scaleOne, 10.f);
                  break;
      }

      case 'y': {
                  player.setScale(scaleTwo);
                  break;
      }
      

      case '0': goto exit;
      }
    }

    YSE::System().sleep(100);

    YSE::System().update();
   
  }

exit:
  YSE::System().close();
  return 0;
}