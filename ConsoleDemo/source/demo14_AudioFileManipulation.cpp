#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/*
  If you just want to play a file, use a sound object and supply it with the 
  name of the file you'd like to load and play. But if you want to alter the 
  sound itself before playing it, use the audioBuffer class. After manipulating
  the audio buffer, it can be supplied to one or more sounds and be played.

  You should take care not to alter an audioBuffer while it is actually playing.
  (Actually I think it is possible as long as you don't change the length,
  but it's not tested. The engine will only read the buffer, while your front-end
  does the write operations. Since audio buffers are floats they should be atomic
  on modern systems.)
*/


YSE::DSP::fileBuffer buffer;
YSE::sound sound;
YSE::DSP::highPass hpf;

int main() {
  YSE::System().init();

  if (!buffer.load("countdown.ogg")) {
    std::cout << "sound 'countdown.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  // pass buffer to sound, so that it can be played
  sound.create(buffer);
  hpf.setFrequency(1000);

  std::cout << "This demo shows how a sound buffer can be altered while playing. Please choose: " << std::endl;
  std::cout << "1 to play" << std::endl;
  std::cout << "2 to stop" << std::endl;
  std::cout << "3 multiply buffer by 0.5" << std::endl;
  std::cout << "4 multiply buffer by 2.0" << std::endl;
  std::cout << "5 pass buffer through high pass filter at 1000Hz" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case '1': sound.play();  break;
      case '2': sound.stop();  break;
      case '3': buffer *= 0.5; break;
      case '4': buffer *= 2.0; break;
      case '5': buffer = hpf(buffer); break;
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