#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound s;

int main() {
  YSE::System().init();
  YSE::sound s;
  s.create("countdown.ogg", NULL, true);

  std::cout << "The playhead on a sound can be moved. This is one sample with numbers. You can jump to any number you'd want, or just hear the countdown." << std::endl;
  std::cout << "Use keyboard to jump to a number." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  s.play();

  while (true) {
#ifdef YSE_WINDOWS
    _cprintf_s("Playing at time: %.2f \r", s.getTime() / 44100);

#endif
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case '0': s.setTime(11.2f * 44100); break;
      case '1': s.setTime(10.0f * 44100); break;
      case '2': s.setTime(9.0f * 44100); break;
      case '3': s.setTime(8.0f * 44100); break;
      case '4': s.setTime(6.7f * 44100); break;
      case '5': s.setTime(5.5f * 44100); break;
      case '6': s.setTime(4.3f * 44100); break;
      case '7': s.setTime(3.2f * 44100); break;
      case '8': s.setTime(2.0f * 44100); break;
      case '9': s.setTime(1.0f * 44100); break;

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