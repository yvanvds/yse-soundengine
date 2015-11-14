#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
YSE::DSP::MODULES::phaser phaser;

float range = 0.1;
float frequency = 0.3;

int main() {
  YSE::System().init();

  // load sound
  sound.create("chord.wav", NULL, true);

  if (!sound.isValid()) {
    std::cout << "sound 'my2chords.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  phaser.impact(1);
  phaser.lfoFrequency(0);
  sound.setDSP(&phaser);
  sound.play();

  std::cout << "This example provides 3 sounds and a sweep filter." << std::endl;
  std::cout << "press d/c to in/decrease sweep frequency." << std::endl;
  std::cout << "press f/v to in/decrease range." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'd': frequency += 0.05; phaser.frequency(frequency); break;
      case 'c': frequency -= 0.05; phaser.frequency(frequency); break;
      case 'f': range += 0.05; phaser.range(range); break;
      case 'v': range -= 0.05; phaser.range(range); break;

      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS
     _cprintf_s("Range: %.2f, Frequency: %.2f \r", phaser.range(), phaser.frequency());
#endif
  }

exit:
  YSE::System().close();
  return 0;
}