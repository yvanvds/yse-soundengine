#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/*
About streaming files
=====================
Advantages:
Streaming from disk is faster to load because only a fraction of the soundfile
is loaded into memory at any time. Because only part of the file is in memory,
it also uses less memory for the file.
Disadvantages:
If you intend to play the file at increased speed, the memory buffer will need
to be replaced much more often. This greatly increases disk usage.
If you loop a file, each part has to be loaded everytime it gets played.
Where non streaming files are loaded into memory only once and can be used by
several instances all playing at different positions or speeds, streaming files
need a buffer for every instance of the file.
Streaming sounds can not be played backwards either.

As a rule of thumb, you should use streaming files for background music or
dialogs. If you may have several instances of the sound playing at once, alter
the speed of the sound or if the sound will be used many times, just load it
into memory.

If in doubt, try loading your sound without streaming and look at the memory
increase of your application in the task manager. If it increases by more than
50 MB, you should at least consider streaming it.

(The memory footprint of a soundfile is more or less equal to it's uncompressed
form, at 44100Hz. Multichannel music can take a lot of bytes!)
*/


YSE::sound sound;

int main() {
  YSE::System().init();

  // setting the last parameter to true will enable streaming
  sound.create("pulse1.ogg", NULL, true, 1.0f, true);
  if (!sound.isValid()) {
    std::cout << "sound 'pulse1.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  std::cout << "This demonstrates the use of streaming sounds from disk instead of loading them into memory before playing." << std::endl;
  std::cout << "Use q/a to change the sound speed up and down." << std::endl;
  std::cout << "Use s/d/f to pause/stop/play." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  sound.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'q': sound.setSpeed(sound.getSpeed() + 0.01); break;
      case 'a': sound.setSpeed(sound.getSpeed() - 0.01); break;
      case 's': sound.pause(); break;
      case 'd': sound.fadeAndStop(3000); break;
      case 'f': sound.setVolume(1).play();  break;
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
