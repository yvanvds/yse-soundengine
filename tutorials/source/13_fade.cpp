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
  YSE::System.init();
  sound.create("drone.ogg", NULL, true).play();

  std::cout << "Sound starts with volume 1.             "      << std::endl;
  std::cout << "Press 1 to fade to 1.0 over 2   seconds."      << std::endl;
  std::cout << "Press 2 to fade to 0.0 over 3.5 seconds."      << std::endl;
  std::cout << "Press 3 to fade to 0.2 over 1   second. "      << std::endl;
  std::cout << "Press 4 to fade to 0.8 instantly.       "      << std::endl;
  std::cout << "...or e to exit."                              << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case '1': sound.volume(1.0, 2000); break;
        case '2': sound.volume(0.0, 3500); break;
        case '3': sound.volume(0.2, 1000); break;
        case '4': sound.volume(0.8, 0000); break;
        case 'e': goto exit;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
  }    

exit:
  YSE::System.close();
  return 0;
}
