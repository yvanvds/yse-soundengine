#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/*
Motifs are short musical ideas which can be fed to a player object. The player also
includes several functions to alter the way the motif is used, along with more general
functions to alter its behaviour.
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
YSE::player player1;
YSE::player player2;
YSE::Vec soundPos;
YSE::scale scale1;
YSE::motif motif1;
YSE::motif motif2;

// this piece works with a timer
Flt progress = 0;

// a simple enum to keep track of the current task
enum STAGES {
  STEP0,
  STEP1,
  STEP2,
  STEP3,
  STEP4,
  STEP5,
  STEP6,
  STEP7,
  STEP8,
  STEP9,
  STEP10,
  STEP11,
  STEP12,
  STEP13,
  STEP14,
  STEP15,
  STEP16,
  STEP17,
  STEP18,
  STEP19,
};

STAGES stages = STEP0;

int main() {
  YSE::System().init();

  YSE::Randomize();

  // create a synth with 32 voices
  synth.create();
  SineWaveVoice voice;
  synth.addVoices(&voice, 32, 1);

  // use this synth as a sound emitter
  sound.create(synth).play();
  soundPos.set(5.f, 0.f, 1.f);
  sound.setPosition(soundPos);

  // create 2 players which uses the synth, and set some starting values
  player1.create(synth);
  player2.create(synth);
  
  player1.setMinimumPitch(60).setMaximumPitch(72);
  player1.setMinimumLength(0.1).setMaximumLength(0.2);
  player1.setMinimumVelocity(0.2).setMaximumVelocity(0.4);
  player1.setVoices(1);

  player2.setMinimumPitch(40).setMaximumPitch(50);
  player2.setMinimumLength(2.f).setMaximumLength(4.f);
  player2.setMinimumVelocity(0.9f).setMaximumVelocity(1.0f);

  // create a scale which can be used by a player
  scale1.add(YSE::NOTE::C4)
    .add(YSE::NOTE::D4)
    .add(YSE::NOTE::E4)
    .add(YSE::NOTE::F4)
    .add(YSE::NOTE::G4)
    .add(YSE::NOTE::A4)
    .add(YSE::NOTE::B4);
  player1.setScale(scale1);
  player2.setScale(scale1);

  // create two distinct motifs
  motif1.add(YSE::MUSIC::pNote(0, YSE::NOTE::C4, 1.0f, 0.25))
    .add(YSE::MUSIC::pNote(0.25, YSE::NOTE::D4, 0.9f, 0.25))
    .add(YSE::MUSIC::pNote(0.5, YSE::NOTE::E4, 0.8f, 0.25))
    .add(YSE::MUSIC::pNote(0.5, YSE::NOTE::A4, 0.8f, 0.25))
    .add(YSE::MUSIC::pNote(0.75, YSE::NOTE::F4, 0.7f, 0.25))
    .add(YSE::MUSIC::pNote(1.0, YSE::NOTE::G4, 0.6f, 0.25))
    .setLength();

  motif2.add(YSE::MUSIC::pNote(0, YSE::NOTE::A5, 0.8f, 0.25))
    .add(YSE::MUSIC::pNote(0.25, YSE::NOTE::G5, 0.7f, 0.25))
    .add(YSE::MUSIC::pNote(0.50, YSE::NOTE::F5, 0.6f, 0.25))
    .add(YSE::MUSIC::pNote(0.75, YSE::NOTE::E5, 0.6f, 1.25))
    .setLength();

  // these scales are used as a group of possible starting pitches for the
  // motif. Transpositions will only happen to these pitches if such a 
  // scale is provided.
  YSE::scale motif1Scale;
  motif1Scale.add(YSE::NOTE::C4);
  motif1.setFirstPitch(motif1Scale);

  YSE::scale motif2Scale;
  motif2Scale.add(YSE::NOTE::A5);
  motif2.setFirstPitch(motif2Scale);

  // instruct player 1 to play this motif. Notice it has a weight of 5. In STEP0 we
  // will add another motif with a weight of 1. This results in this first motif being
  // played most of the time.
  player1.addMotif(motif1, 5).playMotifs(1);

  // add some reverb
  YSE::System().getGlobalReverb().setActive(true);
  YSE::System().getGlobalReverb().setPreset(YSE::REVERB_HALL);
  YSE::ChannelMaster().attachReverb();

  // the interface
  std::cout << "This demo doesn't give you a lot of options, but it will " 
               "evolve on its own. It is an example of the player class, which can be used to generate algorithmic music." << std::endl;
  std::cout << "1  : start player" << std::endl;
  std::cout << "2  : stop player" << std::endl;
  std::cout << "e  : exit" << std::endl;

  Int counter = 0;
  while (true) {

    if (_kbhit()) {
      char ch = _getch();
      YSE::Vec pos = YSE::Listener().getPosition();
      switch (ch) {
      case '1': player1.play(); break;
      case '2': {
                  player1.stop();
                  player2.stop();
                  progress = 0;
                  stages = STEP0;
                  break;
      }
      

      case 'e': goto exit;
      }
    }

    if (player1.isPlaying() || player2.isPlaying()) progress += 0.1;

    // This switch adds changes to the music every 10 seconds
    switch (stages) {
    case STEP0:
      if (progress > 10) {
        std::cout << "step 1 started." << std::endl;
        std::cout << " --> second motif added" << std::endl;
        player1.addMotif(motif2, 1);
        stages = STEP1;

      }
      break;

    case STEP1:
      if (progress > 20) {
        motif1Scale.add(YSE::NOTE::G5);
        player1.setVoices(3, 10).setMaximumPitch(YSE::NOTE::C7, 20);
        std::cout << "step 2 started." << std::endl;
        std::cout << " --> add more voices and play higher" << std::endl;
        stages = STEP2;

      }
      break;

    case STEP2:
      if (progress > 30) {
        player2.play();
        player2.setVoices(3, 10);
        std::cout << "step 3 started." << std::endl;
        std::cout << " --> add some bass" << std::endl;
        stages = STEP3;

      }
      break;

    case STEP3:
      if (progress > 40) {
        player1.playMotifs(0.5, 10).setMinimumLength(2.f).setMaximumLength(4.f);
        player1.setVoices(8, 20);
        std::cout << "step 4 started." << std::endl;
        std::cout << " --> add more voices to player 1" << std::endl;
        std::cout << " --> have player 1 play random notes half of the time" << std::endl;
        stages = STEP4;

      }
      break;

    case STEP4:
      if (progress > 50) {
        std::cout << "step 5 started." << std::endl;
        std::cout << " --> add new notes to the scale" << std::endl;
        std::cout << " --> play less motifs and more random notes" << std::endl;
        scale1.add(YSE::NOTE::EF4).add(YSE::NOTE::BF4);
        player1.playMotifs(0.2, 10).setMinimumPitch(YSE::NOTE::C5, 20);
        stages = STEP5;

      }
      break;

    case STEP5:
      if (progress > 60) {
        std::cout << "step 6 started." << std::endl;
        std::cout << " --> add new notes to the scale" << std::endl;
        std::cout << " --> add new first pitches to motif 2" << std::endl;
        std::cout << " --> remove motif 1 from the player" << std::endl;
        std::cout << " --> add rests in player 2 (bass)" << std::endl;
        scale1.add(YSE::NOTE::FS4).add(YSE::NOTE::CS4);
        motif2Scale.add(YSE::NOTE::C4).add(YSE::NOTE::CS4).add(YSE::NOTE::D4);
        player1.removeMotif(motif1);
        player2.setMinimumGap(2, 10).setMaximumGap(5, 20);
        player2.setVoices(1);
        stages = STEP6;

      }
      break;

    case STEP6:
      if (progress > 70) {
        std::cout << "step 7 started." << std::endl;
        std::cout << " --> play more motifs, but mostly partial" << std::endl;
        player1.playMotifs(1.0, 5);
        player1.playPartialMotifs(0.5, 10);
        player2.stop();
        stages = STEP7;

      }
      break;

    case STEP7:
      if (progress > 80) {
        std::cout << "step 8 started." << std::endl;
        std::cout << " --> play lower notes" << std::endl;
        std::cout << " --> add more voices" << std::endl;
        std::cout << " --> play louder" << std::endl;
        player1.setMinimumPitch(30, 10);
        player1.setVoices(20, 20);
        player1.setMinimumVelocity(0.9, 10).setMaximumVelocity(1.0, 10);
        stages = STEP8;

      }
      break;

    case STEP8:
      if (progress > 90) {
        std::cout << "step 9 started." << std::endl;
        std::cout << " --> have player 2 play wide chords at random" << std::endl;
        stages = STEP9;
        player2.setVoices(15);
        player2.setMinimumLength(3).setMaximumLength(5);
        player2.setMinimumVelocity(0.9).setMaximumVelocity(1.0);
        player2.setMinimumPitch(20).setMaximumPitch(100);
        player2.setMinimumGap(0).setMaximumGap(0);
        player2.playMotifs(0);
        player2.play();
      }
      break;

    case STEP9:
      if (progress > 100) {
        std::cout << "step 10 started." << std::endl;
        std::cout << " --> player 1 stops" << std::endl;
        std::cout << " --> player 2 plays even more long notes" << std::endl;
        stages = STEP10;
        player1.stop();
        player2.setVoices(30);
      }
      break;

    case STEP10:
      if (progress > 110) {
        std::cout << "step 11 started." << std::endl;
        std::cout << " --> reset player 1 to motif 1" << std::endl;
        std::cout << " --> fade out player 2" << std::endl;
        stages = STEP11;
        player1.removeMotif(motif2).addMotif(motif1);
        player1.setVoices(1);
        player1.setMinimumVelocity(0.4).setMaximumVelocity(0.6);
        player1.playMotifs(1).playPartialMotifs(0);
        player1.play();
        player2.setMinimumVelocity(0.1, 10).setMaximumVelocity(0.2, 10);
        player2.setMinimumPitch(70, 10);
      }
      break;

    case STEP11:
      if (progress > 120) {
        std::cout << "step 12 started." << std::endl;
        std::cout << " --> player 2 stops" << std::endl;
        std::cout << " --> player 1 players only partial motifs" << std::endl;
        player2.stop();
        motif1Scale.remove(YSE::NOTE::G5);
        player1.playPartialMotifs(1, 10);
        scale1.remove(YSE::NOTE::FS4).remove(YSE::NOTE::CS4);
        scale1.remove(YSE::NOTE::EF4).remove(YSE::NOTE::BF4);
        player1.setVoices(4, 10);
        stages = STEP12;

      }
      break;

    case STEP12:
      if (progress > 130) {
        std::cout << "step 13 started." << std::endl;
        std::cout << " --> nothing new" << std::endl;
        stages = STEP13;

      }
      break;

    case STEP13:
      if (progress > 140) {
        std::cout << "step 14 started." << std::endl;
        std::cout << " --> player 1 plays louder" << std::endl;
        player1.setMinimumVelocity(0.8, 10).setMaximumVelocity(1.0, 10);
        stages = STEP14;

      }
      break;

    case STEP14:
      if (progress > 150) {
        std::cout << "step 15 started." << std::endl;
        std::cout << " --> nothing new" << std::endl;
        stages = STEP15;

      }
      break;

    case STEP15:
      if (progress > 160) {
        std::cout << "step 16 started." << std::endl;
        std::cout << " --> add short 'F' repetitions on player 2" << std::endl;
        scale1.clear();
        scale1.add(YSE::NOTE::F4);
        player2.setMinimumLength(0.1).setMaximumLength(0.2);
        player2.setMinimumGap(0.1).setMaximumGap(0.2);
        player2.setMinimumVelocity(0.9, 10).setMaximumVelocity(1.0,10);
        player2.setMinimumPitch(70).setMaximumPitch(100);
        player2.setVoices(15);
        player2.play();
        stages = STEP16;

      }
      break;

    case STEP16:
      if (progress > 170) {
        std::cout << "step 17 started." << std::endl;
        std::cout << " --> player 2 add 'B'" << std::endl;
        stages = STEP17;
        scale1.add(YSE::NOTE::B4);
        
      }
      break;

    case STEP17:
      if (progress > 180) {
        std::cout << "step 18 started." << std::endl;
        std::cout << " --> no more partial motifs on player 1" << std::endl;
        std::cout << " --> set player 1 to only one voice" << std::endl;
        std::cout << " --> player 2 fade out" << std::endl;
        stages = STEP18;
        player1.playPartialMotifs(0, 5);
        player1.setVoices(1, 5);
        player2.setMinimumVelocity(0.1, 10).setMaximumVelocity(2.0, 10);
      }
      break;

    case STEP18:
      if (progress > 190) {
        std::cout << "step 19 started." << std::endl;
        std::cout << " --> player 2 stops again" << std::endl;
        player2.stop();
        stages = STEP19;

      }
      break;

    case STEP19:
      if (progress > 200) {
        std::cout << "THE END." << std::endl;
        player1.stop();
        player2.stop();
        stages = STEP0;
        progress = 0;
      }
      break;
    }

    YSE::System().sleep(100);

    YSE::System().update();

  }

exit:
  YSE::System().close();
  return 0;
}