#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#if defined YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;

int main() {
  // initialize audio system
  YSE::System().init();

  // load a sound in memory
  sound.create("drone.ogg", NULL, true);

  // false on validation means the sound could not be loaded
  if (!sound.isValid()) {
    std::cout << "sound 'drone.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  std::cout << "This is a bare-bones YSE example. It contains the minimum you need to start your own projects." << std::endl;
  std::cout << "Press spacebar to toggle sound playing." << std::endl;
  std::cout << "Or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
        // toggle function toggles play / pause
      case ' ': sound.toggle(); break;
      case 'a': sound.play(); break;
      case 's': sound.pause(); break;
      case 'd': sound.stop(); break;
      case 'e': goto exit;
      }
    }

    /* the sleep function can be used to make sure the update function doesn't run at full
    speed. In a simple demo it does not make sense to update that much. In real software
    this should probably not be used. Just call YSE::System.update() in your main program loop.
    */
    YSE::System().sleep(100);
    
    /* The update function activates all changes you made to sounds and channels since the last call.
    This function locks the audio processing thread for a short time. Calling it more than 50 times
    a second is really overkill, so call it once in your main program loop, not after changing every setting.
    */
    YSE::System().update();
  }

exit:
  // don't forget this...
  YSE::System().close();
  return 0;
}
