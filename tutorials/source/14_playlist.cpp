#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::playlist list;
std::vector<std::string> contents;

int main() {
  YSE::System.init();
  list.add("contact.ogg").add("countdown.ogg").add("pulse1.ogg").streamed(true);

  std::cout << "Press q to skip to another song from the list." << std::endl;
  std::cout << "...or e to exit."                               << std::endl;

  list.play();

  while (true) {
#ifdef WINDOWS
    _cprintf_s("Playing at time: %.2f \r", list.time() / 44100 * 1000);
#endif
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'q': list.play(); break;
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
