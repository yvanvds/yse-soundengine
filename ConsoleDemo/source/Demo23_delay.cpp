#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
YSE::DSP::MODULES::highPassDelay delay;

float range = 0.1;
float frequency = 0.3;

int main() {
  YSE::System().init();

  // load sound
  sound.create("c.wav", NULL, true);

  if (!sound.isValid()) {
    std::cout << "sound 'c.wav' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  delay.set(YSE::DSP::MODULES::basicDelay::FIRST, 500, 0.7);
  delay.set(YSE::DSP::MODULES::basicDelay::SECOND, 1100, 0.5);
  delay.set(YSE::DSP::MODULES::basicDelay::THIRD, 1700, 0.3);
  delay.frequency(4000); // frequency of highpass filter

  sound.setDSP(&delay);
  sound.play();

  std::cout << "This example provides 3 sounds and a sweep filter." << std::endl;
  std::cout << "press d/c to in/decrease sweep frequency." << std::endl;
  std::cout << "press f/v to in/decrease range." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {


      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS
    //_cprintf_s("Range: %.2f, Frequency: %.2f \r", phaser.range(), phaser.frequency());
#endif
  }

exit:
  YSE::System().close();
  return 0;
}