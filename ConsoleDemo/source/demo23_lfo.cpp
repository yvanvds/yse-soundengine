#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
YSE::DSP::MODULES::lowPassFilter lp;
Flt frequency = 2;
Flt impact = 0;

int main() {
  YSE::System().init();

  // load sound
  sound.create("chord.wav", NULL, true);

  if (!sound.isValid()) {
    std::cout << "sound 'my2chords.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  lp.frequency(YSE::DSP::MidiToFreq(30));
  lp.impact(impact);
  lp.lfoFrequency(0);
  lp.lfoType(YSE::DSP::LFO_NONE);
  sound.setDSP(&lp);
  sound.play();

  std::cout << "This example provides a chord with a lowpass filter." << std::endl;
  std::cout << "press f/v to in/decrease lfo frequency" << std::endl;
  std::cout << "press d/c to in/decrease lowpass impact" << std::endl;
  std::cout << "press 1 for no lfo" << std::endl;
  std::cout << "press 2 for sawtooth lfo shape" << std::endl;
  std::cout << "press 3 for reversed sawtooth lfo shape" << std::endl;
  std::cout << "press 4 for triangle lfo shape" << std::endl;
  std::cout << "press 5 for square lfo shape" << std::endl;
  std::cout << "press 6 for sine lfo shape" << std::endl;
  std::cout << "press 7 for random lfo shape" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'f': {
                  if (frequency < 32) {
                    frequency += 0.1;
                    lp.lfoFrequency(frequency);
                  }
                  break;
      }
      
      case 'v': {
                  if (frequency > 0) {
                    frequency -= 0.1;
                    lp.lfoFrequency(frequency);
                  }
                  break;
      }

      case 'd': {
                  if (impact < 1) {
                    impact += 0.1;
                    lp.impact(impact);
                  }
                  break;
      }
      case 'c': {
                  if (impact > 0) {
                    impact -= 0.1;
                    lp.impact(impact);
                  }
                  break;
      }

      case '1': lp.lfoType(YSE::DSP::LFO_NONE); break;
      case '2': lp.lfoType(YSE::DSP::LFO_SAW); break;
      case '3': lp.lfoType(YSE::DSP::LFO_SAW_REVERSED); break;
      case '4': lp.lfoType(YSE::DSP::LFO_TRIANGLE); break;
      case '5': lp.lfoType(YSE::DSP::LFO_SQUARE); break;
      case '6': lp.lfoType(YSE::DSP::LFO_SINE); break;
      case '7': lp.lfoType(YSE::DSP::LFO_RANDOM); break;

      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS
     _cprintf_s("Frequency: %.2f Impact: %.2f \r", lp.lfoFrequency(), lp.impact());
    

#endif
  }

exit:
  YSE::System().close();
  return 0;
}