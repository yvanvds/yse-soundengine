#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

const Int maxSounds = 1000;
YSE::sound * sounds[maxSounds];
Int currentSound = 0;

void addSound();

void error(const char * message) {
  std::cout << message << std::endl;
}

int main() {
  YSE::System.init();
  YSE::Error.setCallback(error);

  /* Using 200 sounds is a bit much, but done here for demonstration purposes.
     If the number of active sounds is > maxSounds, YSE will find the most significant
     sounds and virtualize the others.
  */
  YSE::System.maxSounds(200);

  std::cout << "Press the spacebar to add a sound at a random position." << std::endl;
  std::cout << "...or e to exit."                                        << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case ' ': if (currentSound  < maxSounds) addSound(); break;
        case 'e': goto exit;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
#ifdef WINDOWS
    _cprintf_s("Sounds: %d / Audio thread CPU Load: %.2f \r", currentSound, YSE::System.cpuLoad());
#endif
  }    

exit:
  for (Int i = 0; i < maxSounds; i++) {
    delete sounds[i];
  }

  YSE::System.close();
  return 0;
}

void addSound() {
  sounds[currentSound] = new YSE::sound;

  switch(YSE::Random(4)) {
    case 0: sounds[currentSound]->create("contact.ogg", NULL, true); break;
    case 1: sounds[currentSound]->create("drone.ogg"  , NULL, true); break;
    case 2: sounds[currentSound]->create("kick.ogg"   , NULL, true); break;
    case 3: sounds[currentSound]->create("pulse1.ogg" , NULL, true); break;
  }
  if (sounds[currentSound]->valid()) {
    sounds[currentSound]->pos(YSE::Vec(YSE::Random(20) - 10, YSE::Random(20) - 10, YSE::Random(20) - 10));
    sounds[currentSound]->play().volume(0.1); // it can get very loud with 100's of sounds
  }
  currentSound++;
#ifdef MAC
  printf("Sounds: %d / Audio thread CPU Load: %.2f \r", currentSound, YSE::System.cpuLoad());
#endif
}
