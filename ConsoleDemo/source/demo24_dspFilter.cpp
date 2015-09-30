#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
YSE::DSP::MODULES::difference filter;
Flt parm1 = 60;
Flt parm2 = 0;

int main() {
  YSE::System().init();

  // load sound
  sound.create("chord.wav", NULL, true);

  if (!sound.isValid()) {
    std::cout << "sound 'my2chords.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }


  sound.setDSP(&filter);
  filter.frequency(YSE::DSP::MidiToFreq(parm1)).amplitude(parm2);
  sound.play();

  std::cout << "This example provides a sound with a dsp filter." << std::endl;
  std::cout << "press d/c to in/decrease paramter 1" << std::endl;
  std::cout << "press f/v to in/decrease paramter 2" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'd': parm1 += 5; filter.frequency(YSE::DSP::MidiToFreq(parm1)); break;
      case 'c': parm1 -= 5; filter.frequency(YSE::DSP::MidiToFreq(parm1)); break;
      case 'f': parm2 += 0.1; filter.amplitude(parm2); break;
      case 'v': parm2 -= 0.1; filter.amplitude(parm2); break;
      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS
    
    _cprintf_s("Parm1: %.2f, Parm2: %.2f \r", filter.frequency(), filter.amplitude());
    //_cprintf_s("Parm1: %.2f \r", filter.frequency());

#endif
  }

exit:
  YSE::System().close();
  return 0;
}