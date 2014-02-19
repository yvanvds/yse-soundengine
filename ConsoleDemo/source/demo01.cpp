#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;

int main() {
  YSE::System().init();
  sound.create("contact.ogg", NULL, true);
  if (!sound.isValid()) {
    std::cout << "sound 'contact.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  std::cout << "Use q/a to change the sound speed up and down." << std::endl;
  std::cout << "Use w/s to change the volume up and down." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  sound.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'q': sound.setSpeed(sound.getSpeed() + 0.01); break;
      case 'a': sound.setSpeed(sound.getSpeed() - 0.01); break;
      case 's': sound.setVolume(sound.getVolume() - 0.1); break;
      case 'w': sound.setVolume(sound.getVolume() + 0.1); break;
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
