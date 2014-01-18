#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound s;

int main() {
  YSE::System.init();
  YSE::sound s;
  s.create("countdown.ogg", NULL, true);

  std::cout << "Use keyboard to jump to a number." << std::endl;
  std::cout << "...or e to exit."                  << std::endl;
  std::cout << "This sound is " << s.length() / 44100 << " seconds long." << std::endl;
  

  s.play();

  while (true) {
#ifdef WINDOWS
    _cprintf_s("Playing at time: %.2f \r", s.time() / 44100 * 1000);
#endif
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case '0': s.time(11.2f * 44100); break;
        case '1': s.time(10.0f * 44100); break;
        case '2': s.time( 9.0f * 44100); break;
        case '3': s.time( 8.0f * 44100); break;
        case '4': s.time( 6.7f * 44100); break;
        case '5': s.time( 5.5f * 44100); break;
        case '6': s.time( 4.3f * 44100); break;
        case '7': s.time( 3.2f * 44100); break;
        case '8': s.time( 2.0f * 44100); break;
        case '9': s.time( 1.0f * 44100); break;
        
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
