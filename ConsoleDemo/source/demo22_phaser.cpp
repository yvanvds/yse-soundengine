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

int main() {
  YSE::System().init();

  // load sound
  sound.create("chord.wav", NULL, true);

  if (!sound.isValid()) {
    std::cout << "sound 'my2chords.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }


  sound.setDSP(&phaser);
  sound.play();

  std::cout << "This example provides 3 sounds and a sweep filter." << std::endl;
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
    //if (currentSound) {
    //  _cprintf_s("Speed: %.2f, Depth: %d, Frequency: %d \r", filter->speed(), filter->depth(), filter->frequency());
    //}

#endif
  }

exit:
  YSE::System().close();
  return 0;
}